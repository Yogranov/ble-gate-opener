#include "yg_io.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "sdkconfig.h"

#define PRESS_MIN_MS 200u
#define PRESS_MAX_MS 5000u
#define TASK_PERIOD_MS 20
#define LED_LEVEL 24            /* Out of 255: it should not light up a dark car. */
#define BEEP_HZ 2700

static const char *TAG = "yg_io";

static esp_timer_handle_t s_press_timer;
static led_strip_handle_t s_strip;
static yg_io_gesture_cb_t s_on_gesture;
static yg_gesture_detector_t s_gesture;

static volatile yg_led_event_t s_led_event;
static volatile bool s_led_changed;
static yg_led_event_t s_led_base;

static volatile uint8_t s_beeps_requested;
static volatile uint16_t s_beep_on_ms;
static volatile uint8_t s_beep_type;
static bool s_ledc_ready;

static void press_end(void *arg) {
    (void)arg;
    gpio_set_level(CONFIG_YG_PIN_REMOTE, 0);
}

void yg_io_remote_init(void) {
    gpio_set_level(CONFIG_YG_PIN_REMOTE, 0);
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << CONFIG_YG_PIN_REMOTE,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&config);
    gpio_set_level(CONFIG_YG_PIN_REMOTE, 0);

    const esp_timer_create_args_t timer = {
        .callback = press_end,
        .name = "yg_press",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer, &s_press_timer));
}

void yg_io_press(uint32_t ms) {
    if (ms < PRESS_MIN_MS) ms = PRESS_MIN_MS;
    if (ms > PRESS_MAX_MS) ms = PRESS_MAX_MS;
    esp_timer_stop(s_press_timer);
    gpio_set_level(CONFIG_YG_PIN_REMOTE, 1);
    ESP_ERROR_CHECK(esp_timer_start_once(s_press_timer, (uint64_t)ms * 1000u));
}

bool yg_io_remote_level(void) {
    return gpio_get_level(CONFIG_YG_PIN_REMOTE) != 0;
}

static void led_color(uint8_t r, uint8_t g, uint8_t b) {
    if (s_strip == NULL) {
        return;
    }
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

static void led_white(void) { led_color(LED_LEVEL, LED_LEVEL, LED_LEVEL); }
static void led_off(void) { led_color(0, 0, 0); }

/* Draws the pattern. Returns false once a one-shot pattern has finished. */
static bool led_draw(yg_led_event_t e, uint32_t ms) {
    switch (e) {
    case YG_LED_PRESS:
        led_white();
        return true;
    case YG_LED_PAIRING:
        led_color(0, 0, (ms / 100) % 2 ? 0 : LED_LEVEL);
        return true;
    case YG_LED_SAVED:
        led_color(0, LED_LEVEL, 0);
        return ms < 2000;
    case YG_LED_REJECTED:
        led_color((ms / 200) % 2 ? 0 : LED_LEVEL, 0, 0);
        return ms < 1200;
    case YG_LED_SENSOR_FAILED: {
        uint32_t t = ms % 10000;
        bool on = t < 150 || (t >= 300 && t < 450);
        led_color(on ? LED_LEVEL : 0, 0, 0);
        return true;
    }
    case YG_LED_UNPAIRED:
        led_color((ms / 500) % 2 ? 0 : LED_LEVEL, (ms / 500) % 2 ? 0 : LED_LEVEL / 2, 0);
        return ms < 30000;
    default:
        led_off();
        return true;
    }
}

static void buzzer_output(bool on, uint8_t type) {
    if (type == YG_BUZZER_ACTIVE) {
        gpio_set_level(CONFIG_YG_PIN_BUZZER, on);
        return;
    }
    if (!s_ledc_ready) {
        const ledc_timer_config_t timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_10_BIT,
            .timer_num = LEDC_TIMER_0,
            .freq_hz = BEEP_HZ,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        const ledc_channel_config_t channel = {
            .gpio_num = CONFIG_YG_PIN_BUZZER,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_0,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
        };
        if (ledc_timer_config(&timer) != ESP_OK || ledc_channel_config(&channel) != ESP_OK) {
            ESP_LOGW(TAG, "Buzzer setup failed");
            return;
        }
        s_ledc_ready = true;
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, on ? 512 : 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void io_task(void *arg) {
    (void)arg;
    yg_led_event_t current = YG_LED_OFF;
    uint32_t pattern_start = 0;
    uint8_t beeps_left = 0;
    uint32_t beep_next = 0;
    bool beep_on = false;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(TASK_PERIOD_MS));
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

        bool pressed = gpio_get_level(CONFIG_YG_PIN_BUTTON) == 0 ||
                       gpio_get_level(CONFIG_YG_PIN_BUTTON_EXT) == 0;
        yg_gesture_t gesture = yg_gesture_update(&s_gesture, now, pressed);
        if (gesture != YG_GESTURE_NONE && s_on_gesture != NULL) {
            s_on_gesture(gesture);
        }

        if (s_led_changed) {
            s_led_changed = false;
            current = s_led_event;
            pattern_start = now;
            if (current == YG_LED_OFF || current == YG_LED_SENSOR_FAILED) {
                s_led_base = current;
            }
        }
        if (!led_draw(current, now - pattern_start)) {
            current = s_led_base;
            pattern_start = now;
        }

        if (s_beeps_requested > 0 && beeps_left == 0) {
            beeps_left = s_beeps_requested;
            s_beeps_requested = 0;
            beep_next = now;
            beep_on = false;
        }
        if (beeps_left > 0 && (int32_t)(now - beep_next) >= 0) {
            beep_on = !beep_on;
            buzzer_output(beep_on, s_beep_type);
            beep_next = now + (beep_on ? s_beep_on_ms : 120u);
            if (!beep_on) {
                beeps_left--;
            }
        }
    }
}

void yg_io_start(yg_io_gesture_cb_t on_gesture) {
    s_on_gesture = on_gesture;
    yg_gesture_init(&s_gesture);

    const gpio_config_t button = {
        .pin_bit_mask = (1ULL << CONFIG_YG_PIN_BUTTON) | (1ULL << CONFIG_YG_PIN_BUTTON_EXT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&button);

    gpio_set_level(CONFIG_YG_PIN_BUZZER, 0);
    const gpio_config_t buzzer = {
        .pin_bit_mask = 1ULL << CONFIG_YG_PIN_BUZZER,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&buzzer);

    const led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_YG_PIN_LED,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    const led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LED setup failed: %s", esp_err_to_name(err));
        s_strip = NULL;
    }

    xTaskCreate(io_task, "yg_io", 3072, NULL, 5, NULL);
}

void yg_io_led(yg_led_event_t e) {
    s_led_event = e;
    s_led_changed = true;
}

void yg_io_buzz(yg_buzz_event_t e, uint8_t buzzer_type) {
    s_beep_type = buzzer_type;
    switch (e) {
    case YG_BUZZ_SAVED:
        s_beep_on_ms = 80;
        s_beeps_requested = 2;
        break;
    case YG_BUZZ_REJECTED:
        s_beep_on_ms = 600;
        s_beeps_requested = 1;
        break;
    default:
        s_beep_on_ms = 80;
        s_beeps_requested = 1;
        break;
    }
}

void yg_io_test_led(void) {
    yg_io_led(YG_LED_SAVED);
}

void yg_io_test_beep(uint8_t buzzer_type) {
    yg_io_buzz(YG_BUZZ_SAVED, buzzer_type);
}
