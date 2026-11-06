#include "yg_rf.h"

#include "sdkconfig.h"

#if CONFIG_YG_GATE_DRIVER_RF433

#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "yg_rf";

#define RF_SPI_HOST SPI2_HOST
#define RF_SPI_HZ (1 * 1000 * 1000)

/* CC1101 command strobes and the version status register (datasheet SPI section). */
#define CC1101_SRES 0x30
#define CC1101_SRX 0x34
#define CC1101_STX 0x35
#define CC1101_SIDLE 0x36
#define CC1101_SFRX 0x3A
#define CC1101_SFTX 0x3B
#define CC1101_READ 0x80
#define CC1101_BURST 0x40
#define CC1101_REG_VERSION 0x31 /* read as a status register: READ | BURST | addr */
#define CC1101_REG_PATABLE 0x3E
#define CC1101_REG_MARCSTATE 0x35 /* a status register, read like VERSION */
#define CC1101_STATE_RX 0x0D
#define CC1101_STATE_TX 0x13

/* 1 microsecond per RMT tick keeps OOK timing exact. The 15-bit tick field caps a
 * single level at 32767 us, which is wider than any inter-frame gap we expect. */
#define RF_RMT_RES_HZ (1 * 1000 * 1000)
#define RF_RX_MAX_SYMBOLS 512
#define RF_TX_MAX_SYMBOLS 300
#define RF_RX_IDLE_NS (32 * 1000 * 1000) /* an idle this long ends one capture */
/* The C6 receive filter counts 80 MHz group-clock ticks in 8 bits, so it tops out at
 * 3187 ns. rmt_receive refuses anything longer, and then nothing is ever heard. */
#define RF_RX_GLITCH_NS 3000

static spi_device_handle_t s_spi;
static bool s_present;
static rmt_channel_handle_t s_tx_chan;
static rmt_channel_handle_t s_rx_chan;
static rmt_encoder_handle_t s_copy_encoder;
static yg_rf_capture_cb_t s_capture_cb;
static TaskHandle_t s_rx_task;
static rmt_symbol_word_t s_rx_symbols[RF_RX_MAX_SYMBOLS];
static volatile size_t s_rx_num;
static volatile bool s_listening;

/* CC1101 registers for 433.92 MHz OOK, asynchronous serial on GDO0. FIRST GUESS:
 * confirm every value on the bench against a HackRF capture of the real remote
 * (see the design spec's test plan). Pairs of {address, value}. */
static const uint8_t CC1101_CONFIG[][2] = {
    {0x02, 0x0D}, /* IOCFG0: GDO0 = asynchronous serial data in/out */
    {0x08, 0x32}, /* PKTCTRL0: asynchronous serial mode */
    {0x0B, 0x06}, /* FSCTRL1 */
    {0x0D, 0x10}, /* FREQ2  433.92 MHz */
    {0x0E, 0xB0}, /* FREQ1 */
    {0x0F, 0x71}, /* FREQ0 */
    {0x10, 0x87}, /* MDMCFG4: RX bandwidth and data rate */
    {0x11, 0x83}, /* MDMCFG3 */
    {0x12, 0x30}, /* MDMCFG2: ASK/OOK, no sync word */
    {0x15, 0x15}, /* DEVIATN */
    {0x18, 0x18}, /* MCSM0: auto-calibrate on idle->rx/tx */
    /* AGCCTRL2: the radio listens only to learn, with the remote held at the unit, so it
     * trades sensitivity for quiet. At full gain with no carrier, the AGC lifts noise to
     * the OOK decision level and GDO0 chatters: a capture never sees the idle that ends
     * it, and noise fills the gaps between frames. Capping the LNA 17 dB down and the top
     * three DVGA steps keeps noise well under it; a remote across the room is still far
     * above. MAGN_TARGET stays 33 dB. A first guess, to tune on the bench. */
    {0x1B, 0xFB},
    {0x21, 0xB6}, /* FREND1 */
    {0x22, 0x11}, /* FREND0 */
    {0x23, 0xE9}, /* FSCAL3 */
    {0x24, 0x2A}, /* FSCAL2 */
    {0x25, 0x00}, /* FSCAL1 */
    {0x26, 0x1F}, /* FSCAL0 */
    /* TEST2..0: SmartRF Studio's values for 433.92 MHz at this bandwidth, from the same
     * export as FSCAL3..0. The datasheet says to take them from it. Reset leaves TEST0's
     * VCO selection calibration on, which that export turns off. */
    {0x2C, 0x81},
    {0x2D, 0x35},
    {0x2E, 0x09},
};

static void cc1101_strobe(uint8_t cmd) {
    spi_transaction_t t = {.length = 8, .tx_buffer = &cmd};
    spi_device_polling_transmit(s_spi, &t);
}

static void cc1101_write(uint8_t addr, uint8_t value) {
    uint8_t tx[2] = {addr, value};
    spi_transaction_t t = {.length = 16, .tx_buffer = tx};
    spi_device_polling_transmit(s_spi, &t);
}

/* OOK sends PATABLE[0] for a 0 and PATABLE[1] for a 1 (FREND0.PA_POWER = 1). Reset leaves
 * 0xC6 then 0x00, which would key the carrier on in the gaps and off in the pulses. */
static void cc1101_write_patable(void) {
    const uint8_t tx[3] = {CC1101_REG_PATABLE | CC1101_BURST, 0x00, 0xC0}; /* off, +10 dBm at 433 MHz */
    spi_transaction_t t = {.length = 24, .tx_buffer = tx};
    spi_device_polling_transmit(s_spi, &t);
}

static uint8_t cc1101_read_status(uint8_t addr) {
    uint8_t tx[2] = {(uint8_t)(addr | CC1101_READ | CC1101_BURST), 0};
    uint8_t rx[2] = {0};
    spi_transaction_t t = {.length = 16, .tx_buffer = tx, .rx_buffer = rx};
    spi_device_polling_transmit(s_spi, &t);
    return rx[1];
}

/* Calibration on the way from idle to RX or TX takes about 0.8 ms, and GDO0 means
 * nothing until it is done: a transmit started early clips its first frame. */
static void cc1101_wait_state(uint8_t state) {
    for (int i = 0; i < 40; i++) {
        if ((cc1101_read_status(CC1101_REG_MARCSTATE) & 0x1F) == state) {
            return;
        }
        esp_rom_delay_us(50);
    }
    ESP_LOGW(TAG, "CC1101 did not reach state 0x%02X", state);
}

static void cc1101_configure(void) {
    cc1101_strobe(CC1101_SRES);
    /* A busy wait: at the 100 Hz tick, pdMS_TO_TICKS(1) is zero and would not wait at all,
     * so the configuration could go out while the chip is still resetting. */
    esp_rom_delay_us(1000);
    for (size_t i = 0; i < sizeof(CC1101_CONFIG) / sizeof(CC1101_CONFIG[0]); i++) {
        cc1101_write(CC1101_CONFIG[i][0], CC1101_CONFIG[i][1]);
    }
    cc1101_write_patable();
    cc1101_strobe(CC1101_SIDLE);
}

static void start_receive(void) {
    rmt_receive_config_t rc = {
        .signal_range_min_ns = RF_RX_GLITCH_NS,
        .signal_range_max_ns = RF_RX_IDLE_NS,
    };
    esp_err_t err = rmt_receive(s_rx_chan, s_rx_symbols, sizeof(s_rx_symbols), &rc);
    /* Invalid state only means a receive is already running. Anything else means learning
     * cannot hear, so say so rather than time out in silence. */
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "rmt_receive failed: %s", esp_err_to_name(err));
    }
}

/* ISR: the received_symbols buffer is our own s_rx_symbols, so just record the
 * count and wake the task, which reads it before the next receive overwrites it. */
static bool IRAM_ATTR rx_done(rmt_channel_handle_t chan, const rmt_rx_done_event_data_t *edata, void *ctx) {
    (void)chan;
    (void)ctx;
    s_rx_num = edata->num_symbols;
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_rx_task, &woken);
    return woken == pdTRUE;
}

static void rx_task(void *arg) {
    (void)arg;
    static uint16_t durations[RF_RX_MAX_SYMBOLS * 2];
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        size_t n = s_rx_num;
        if (n > RF_RX_MAX_SYMBOLS) {
            n = RF_RX_MAX_SYMBOLS;
        }
        uint16_t count = 0;
        bool first_high = n > 0 && s_rx_symbols[0].level0 != 0;
        for (size_t i = 0; i < n; i++) {
            /* A zero duration is the end marker, in either half of a symbol. */
            if (s_rx_symbols[i].duration0 == 0) {
                break;
            }
            durations[count++] = (uint16_t)s_rx_symbols[i].duration0;
            if (s_rx_symbols[i].duration1 == 0) {
                break;
            }
            durations[count++] = (uint16_t)s_rx_symbols[i].duration1;
        }
        if (s_listening && s_capture_cb != NULL && count > 0) {
            s_capture_cb(durations, count, first_high);
        }
        if (s_listening) {
            start_receive();
        }
    }
}

void yg_rf_init(void) {
    /* CSn idles high (deselected) from the first moment, before SPI takes the pin. */
    const gpio_config_t cs = {
        .pin_bit_mask = 1ULL << CONFIG_YG_PIN_RF_CS,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cs);
    gpio_set_level(CONFIG_YG_PIN_RF_CS, 1);

    const spi_bus_config_t bus = {
        .mosi_io_num = CONFIG_YG_PIN_RF_MOSI,
        .miso_io_num = CONFIG_YG_PIN_RF_MISO,
        .sclk_io_num = CONFIG_YG_PIN_RF_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64,
    };
    if (spi_bus_initialize(RF_SPI_HOST, &bus, SPI_DMA_DISABLED) != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed");
        return;
    }
    const spi_device_interface_config_t dev = {
        .clock_speed_hz = RF_SPI_HZ,
        .mode = 0,
        .spics_io_num = CONFIG_YG_PIN_RF_CS,
        .queue_size = 1,
    };
    if (spi_bus_add_device(RF_SPI_HOST, &dev, &s_spi) != ESP_OK) {
        ESP_LOGE(TAG, "SPI add device failed");
        return;
    }

    cc1101_configure();
    uint8_t version = cc1101_read_status(CC1101_REG_VERSION);
    s_present = version != 0x00 && version != 0xFF;
    ESP_LOGI(TAG, "CC1101 version 0x%02X, %s", version, s_present ? "present" : "not found");

    /* TX and RX share the one GDO0 data pin; only one is enabled at a time.
     * mem_block_symbols is kept small; a bench check decides if DMA or partial
     * receive is needed for long captures. */
    const rmt_tx_channel_config_t txc = {
        .gpio_num = CONFIG_YG_PIN_RF_GDO0,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RF_RMT_RES_HZ,
        /* One C6 block of 48. The C6 has two TX channels, and 64 would borrow the second,
         * which the developer LED's led_strip needs. */
        .mem_block_symbols = 48,
        .trans_queue_depth = 2,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&txc, &s_tx_chan));
    /* A TX channel drives its pin low from creation, even while disabled, but the CC1101
     * drives GDO0 itself in idle and RX. Let go of the pin until a transmit needs it. */
    gpio_output_disable(CONFIG_YG_PIN_RF_GDO0);
    const rmt_copy_encoder_config_t enc = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&enc, &s_copy_encoder));

    const rmt_rx_channel_config_t rxc = {
        .gpio_num = CONFIG_YG_PIN_RF_GDO0,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RF_RMT_RES_HZ,
        .mem_block_symbols = 64,
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rxc, &s_rx_chan));
    const rmt_rx_event_callbacks_t cbs = {.on_recv_done = rx_done};
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(s_rx_chan, &cbs, NULL));

    xTaskCreate(rx_task, "yg_rf_rx", 4096, NULL, 6, &s_rx_task);
}

bool yg_rf_present(void) {
    return s_present;
}

void yg_rf_transmit(const yg_rfcode_t *code, uint32_t ms) {
    if (!code->learned || code->count == 0) {
        return;
    }
    static uint16_t durations[RF_TX_MAX_SYMBOLS * 2];
    uint16_t n = yg_rfcode_replay(code, ms, durations, (uint16_t)(sizeof(durations) / sizeof(durations[0])));
    if (n == 0) {
        return;
    }
    static rmt_symbol_word_t symbols[RF_TX_MAX_SYMBOLS]; /* two durations each */
    uint16_t sym = 0;
    for (uint16_t i = 0; i < n; i += 2) {
        symbols[sym].level0 = 1;
        symbols[sym].duration0 = durations[i];
        symbols[sym].level1 = 0;
        symbols[sym].duration1 = (uint16_t)((i + 1 < n) ? durations[i + 1] : 0);
        sym++;
    }

    bool was_listening = s_listening;
    if (was_listening) {
        rmt_disable(s_rx_chan);
    }
    cc1101_strobe(CC1101_SIDLE);
    /* Take GDO0 back only for the transmit. STX makes it the CC1101's data input. */
    ESP_ERROR_CHECK(rmt_tx_switch_gpio(s_tx_chan, CONFIG_YG_PIN_RF_GDO0, false));
    ESP_ERROR_CHECK(rmt_enable(s_tx_chan));
    cc1101_strobe(CC1101_SFTX);
    cc1101_strobe(CC1101_STX);
    cc1101_wait_state(CC1101_STATE_TX);
    const rmt_transmit_config_t tc = {.loop_count = 0};
    rmt_transmit(s_tx_chan, s_copy_encoder, symbols, (size_t)sym * sizeof(rmt_symbol_word_t), &tc);
    /* A hard limit, as the MOSFET press has. The replay runs at most ms and one frame. */
    if (rmt_tx_wait_all_done(s_tx_chan, (int)ms + 1000) != ESP_OK) {
        ESP_LOGE(TAG, "transmit overran and was cut off");
    }
    cc1101_strobe(CC1101_SIDLE);
    rmt_disable(s_tx_chan);
    gpio_output_disable(CONFIG_YG_PIN_RF_GDO0);

    if (was_listening) {
        ESP_ERROR_CHECK(rmt_enable(s_rx_chan));
        cc1101_strobe(CC1101_SRX);
        cc1101_wait_state(CC1101_STATE_RX);
        start_receive();
    }
}

void yg_rf_listen(bool on, yg_rf_capture_cb_t cb) {
    s_capture_cb = cb;
    if (on && !s_listening) {
        s_listening = true;
        ESP_ERROR_CHECK(rmt_enable(s_rx_chan));
        cc1101_strobe(CC1101_SIDLE);
        cc1101_strobe(CC1101_SFRX);
        cc1101_strobe(CC1101_SRX);
        cc1101_wait_state(CC1101_STATE_RX);
        start_receive();
    } else if (!on && s_listening) {
        s_listening = false;
        cc1101_strobe(CC1101_SIDLE);
        rmt_disable(s_rx_chan);
    }
}

#endif /* CONFIG_YG_GATE_DRIVER_RF433 */

/* Keeps the translation unit non-empty on the MOSFET build, where the body is excluded. */
typedef int yg_rf_placeholder_t;
