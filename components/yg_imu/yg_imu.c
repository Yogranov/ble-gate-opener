#include "yg_imu.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define I2C_HZ        400000
#define I2C_TIMEOUT_MS 20
#define REG_SMPLRT_DIV 0x19
#define REG_CONFIG     0x1A
#define REG_GYRO_CFG   0x1B
#define REG_ACCEL_CFG  0x1C
#define REG_ACCEL_OUT  0x3B
#define REG_PWR_MGMT_1 0x6B
#define REG_WHO_AM_I   0x75
#define ACCEL_LSB_PER_G  16384.0f   /* 2 g range */
#define GYRO_LSB_PER_DPS 131.0f     /* 250 deg/s range */
#define SAMPLE_PERIOD_MS 20
#define REINIT_EVERY_FAILS 50

static const char *TAG = "yg_imu";

static i2c_master_dev_handle_t s_dev;
static yg_imu_sample_cb_t s_on_sample;
static yg_imu_failed_cb_t s_on_failed;

static esp_err_t write_reg(uint8_t reg, uint8_t value) {
    const uint8_t data[2] = { reg, value };
    return i2c_master_transmit(s_dev, data, sizeof(data), I2C_TIMEOUT_MS);
}

static esp_err_t configure(void) {
    /* Wake up on the gyro clock. 50 Hz output, about 10 Hz low-pass filter to
     * remove engine shake, 2 g and 250 deg/s ranges. */
    esp_err_t err = write_reg(REG_PWR_MGMT_1, 0x01);
    if (err == ESP_OK) err = write_reg(REG_SMPLRT_DIV, 19);
    if (err == ESP_OK) err = write_reg(REG_CONFIG, 5);
    if (err == ESP_OK) err = write_reg(REG_GYRO_CFG, 0);
    if (err == ESP_OK) err = write_reg(REG_ACCEL_CFG, 0);
    return err;
}

bool yg_imu_init(uint8_t *id) {
    *id = 0;
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = CONFIG_YG_PIN_SDA,
        .scl_io_num = CONFIG_YG_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    esp_err_t err = i2c_new_master_bus(&bus_config, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus failed: %s", esp_err_to_name(err));
        return false;
    }
    uint16_t address = 0;
    if (i2c_master_probe(bus, 0x68, I2C_TIMEOUT_MS) == ESP_OK) {
        address = 0x68;
    } else if (i2c_master_probe(bus, 0x69, I2C_TIMEOUT_MS) == ESP_OK) {
        address = 0x69;
    } else {
        ESP_LOGW(TAG, "No MPU6050 found on SDA %d, SCL %d", CONFIG_YG_PIN_SDA, CONFIG_YG_PIN_SCL);
        return false;
    }
    const i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = I2C_HZ,
    };
    if (i2c_master_bus_add_device(bus, &dev_config, &s_dev) != ESP_OK) {
        return false;
    }
    const uint8_t reg = REG_WHO_AM_I;
    i2c_master_transmit_receive(s_dev, &reg, 1, id, 1, I2C_TIMEOUT_MS);
    ESP_LOGI(TAG, "Sensor at 0x%02x, WHO_AM_I 0x%02x", (unsigned)address, *id);
    return configure() == ESP_OK;
}

static int16_t be16(const uint8_t *p) {
    return (int16_t)((p[0] << 8) | p[1]);
}

static void imu_task(void *arg) {
    (void)arg;
    /* The sensor needs a moment after waking before its readings are good. */
    vTaskDelay(pdMS_TO_TICKS(100));
    uint32_t fails = 0;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
        const uint8_t reg = REG_ACCEL_OUT;
        uint8_t data[14];
        if (i2c_master_transmit_receive(s_dev, &reg, 1, data, sizeof(data), I2C_TIMEOUT_MS) != ESP_OK) {
            fails++;
            s_on_failed();
            /* A power glitch can reset the sensor to its sleeping defaults. */
            if (fails % REINIT_EVERY_FAILS == 0) {
                configure();
            }
            continue;
        }
        if (fails >= REINIT_EVERY_FAILS) {
            configure();
        }
        fails = 0;
        yg_imu_sample_t s = {
            .ax = (float)be16(data) / ACCEL_LSB_PER_G,
            .ay = (float)be16(data + 2) / ACCEL_LSB_PER_G,
            .az = (float)be16(data + 4) / ACCEL_LSB_PER_G,
            .gx = (float)be16(data + 8) / GYRO_LSB_PER_DPS,
            .gy = (float)be16(data + 10) / GYRO_LSB_PER_DPS,
            .gz = (float)be16(data + 12) / GYRO_LSB_PER_DPS,
        };
        s_on_sample(&s, (float)be16(data + 6) / 340.0f + 36.53f);
    }
}

void yg_imu_start(yg_imu_sample_cb_t on_sample, yg_imu_failed_cb_t on_failed) {
    s_on_sample = on_sample;
    s_on_failed = on_failed;
    xTaskCreate(imu_task, "yg_imu", 3072, NULL, 6, NULL);
}
