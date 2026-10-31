#include "yg_imu.h"

#include <string.h>

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
#define REG_ACCEL_CFG2 0x1D
#define REG_ACCEL_OUT  0x3B
#define REG_PWR_MGMT_1 0x6B
#define REG_WHO_AM_I   0x75
#define ACCEL_LSB_PER_G  16384.0f   /* 2 g range */
#define GYRO_LSB_PER_DPS 131.0f     /* 250 deg/s range */
#define SAMPLE_PERIOD_MS 20
#define WAKE_MS          100         /* The sensor's readings are not good before this. */
#define REINIT_AFTER_FAILS 5         /* A run this long may have been a power glitch. */
#define REINIT_AFTER_FROZEN 50       /* A sensor reset by a power glitch sleeps, and repeats itself. */
#define SEARCH_EVERY_SAMPLES 10      /* Looks for a missing sensor 5 times a second. */
#define RAW_FRAME_SIZE   14

static const char *TAG = "yg_imu";

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static volatile uint8_t s_id;
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
    /* MPU-6500 clones filter the accelerometer separately. Unused on a real MPU6050. */
    if (err == ESP_OK) err = write_reg(REG_ACCEL_CFG2, 5);
    return err;
}

static bool find_sensor(void) {
    uint16_t address;
    if (i2c_master_probe(s_bus, 0x68, I2C_TIMEOUT_MS) == ESP_OK) {
        address = 0x68;
    } else if (i2c_master_probe(s_bus, 0x69, I2C_TIMEOUT_MS) == ESP_OK) {
        address = 0x69;
    } else {
        return false;
    }
    const i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = I2C_HZ,
    };
    if (i2c_master_bus_add_device(s_bus, &dev_config, &s_dev) != ESP_OK) {
        s_dev = NULL;
        return false;
    }
    const uint8_t reg = REG_WHO_AM_I;
    uint8_t id = 0;
    i2c_master_transmit_receive(s_dev, &reg, 1, &id, 1, I2C_TIMEOUT_MS);
    s_id = id;
    ESP_LOGI(TAG, "Sensor at 0x%02x, WHO_AM_I 0x%02x", (unsigned)address, id);
    return true;
}

uint8_t yg_imu_id(void) {
    return s_id;
}

bool yg_imu_init(void) {
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = CONFIG_YG_PIN_SDA,
        .scl_io_num = CONFIG_YG_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus failed: %s", esp_err_to_name(err));
        s_bus = NULL;
        return false;
    }
    if (!find_sensor()) {
        ESP_LOGW(TAG, "No MPU6050 found yet on SDA %d, SCL %d", CONFIG_YG_PIN_SDA, CONFIG_YG_PIN_SCL);
        return false;
    }
    return true;
}

static int16_t be16(const uint8_t *p) {
    return (int16_t)((p[0] << 8) | p[1]);
}

static void imu_task(void *arg) {
    (void)arg;
    uint32_t searches = 0;
    uint32_t fails = 0;
    uint32_t frozen = 0;
    bool needs_config = true;
    uint8_t last[RAW_FRAME_SIZE] = { 0 };
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
        if (s_dev == NULL) {
            if (++searches % SEARCH_EVERY_SAMPLES != 0) {
                continue;
            }
            if (s_bus == NULL || !find_sensor()) {
                s_on_failed(true);
            }
            continue;
        }
        if (needs_config) {
            if (configure() != ESP_OK) {
                s_on_failed(false);
                continue;
            }
            needs_config = false;
            vTaskDelay(pdMS_TO_TICKS(WAKE_MS));
            wake = xTaskGetTickCount();
            continue;
        }
        const uint8_t reg = REG_ACCEL_OUT;
        uint8_t data[RAW_FRAME_SIZE];
        if (i2c_master_transmit_receive(s_dev, &reg, 1, data, sizeof(data), I2C_TIMEOUT_MS) != ESP_OK) {
            s_on_failed(false);
            if (++fails >= REINIT_AFTER_FAILS) {
                needs_config = true;
                fails = 0;
            }
            continue;
        }
        /* Whatever broke the reads may have reset the sensor to its sleeping defaults. A
         * short run needs no wait, as it has most likely not slept. */
        if (fails > 0) {
            configure();
            fails = 0;
        }
        if (memcmp(data, last, sizeof(data)) == 0) {
            if (++frozen >= REINIT_AFTER_FROZEN) {
                needs_config = true;
                frozen = 0;
            }
        } else {
            frozen = 0;
            memcpy(last, data, sizeof(data));
        }
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
    xTaskCreate(imu_task, "yg_imu", 4096, NULL, 6, NULL);
}
