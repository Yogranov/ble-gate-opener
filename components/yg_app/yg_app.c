#include "yg_app.h"

#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "yg_ble.h"
#include "yg_imu.h"
#include "yg_io.h"
#include "yg_log.h"
#include "yg_nvs.h"

#define QUEUE_LENGTH 64
#define TICK_MS 100u
#define OTA_DEADLINE_MS 30000u
#define RARE_EVENT_WAIT pdMS_TO_TICKS(200)

static const char *TAG = "yg_app";

typedef enum
{
    EV_ADVERT,
    EV_IMU,
    EV_IMU_FAILED,
    EV_GESTURE,
    EV_CLIENTS,
    EV_CALL,
} event_kind_t;

typedef struct
{
    event_kind_t kind;
    uint32_t t;
    union
    {
        struct
        {
            yg_addr_t addr;
            int8_t rssi;
            yg_ibeacon_t ib;
        } advert;
        struct
        {
            yg_imu_sample_t sample;
            float temp_c;
        } imu;
        yg_gesture_t gesture;
        uint8_t clients;
        bool absent;
        struct
        {
            yg_app_fn_t fn;
            void *arg;
            SemaphoreHandle_t done;
        } call;
    };
} event_t;

static yg_engine_t s_engine;
static yg_settings_t s_settings;
static yg_beacon_t s_beacon;
static yg_app_hooks_t s_hooks;
static QueueHandle_t s_queue;
static volatile uint32_t s_dropped;
static uint32_t s_dropped_logged;
static volatile uint32_t s_trip;
static bool s_settings_ok;
static bool s_ota_pending;
static bool s_test_fail_self_check;

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void post(event_t *ev, TickType_t wait) {
    ev->t = now_ms();
    if (xQueueSend(s_queue, ev, wait) != pdTRUE) {
        s_dropped++;
    }
}

static void on_advert(const yg_addr_t *a, int8_t rssi, const yg_ibeacon_t *ib) {
    event_t ev = { .kind = EV_ADVERT };
    ev.advert.addr = *a;
    ev.advert.rssi = rssi;
    ev.advert.ib = *ib;
    post(&ev, 0);
}

static void on_imu_sample(const yg_imu_sample_t *s, float temp_c) {
    event_t ev = { .kind = EV_IMU };
    ev.imu.sample = *s;
    ev.imu.temp_c = temp_c;
    post(&ev, 0);
}

static void on_imu_failed(bool absent) {
    event_t ev = { .kind = EV_IMU_FAILED };
    ev.absent = absent;
    post(&ev, 0);
}

static void on_gesture(yg_gesture_t g) {
    event_t ev = { .kind = EV_GESTURE };
    ev.gesture = g;
    post(&ev, RARE_EVENT_WAIT);
}

void yg_app_post_clients(uint8_t clients) {
    event_t ev = { .kind = EV_CLIENTS };
    ev.clients = clients;
    post(&ev, RARE_EVENT_WAIT);
}

void yg_app_call(yg_app_fn_t fn, void *arg) {
    event_t ev = { .kind = EV_CALL };
    ev.call.fn = fn;
    ev.call.arg = arg;
    ev.call.done = xSemaphoreCreateBinary();
    ev.t = now_ms();
    xQueueSend(s_queue, &ev, portMAX_DELAY);
    xSemaphoreTake(ev.call.done, portMAX_DELAY);
    vSemaphoreDelete(ev.call.done);
}

uint32_t yg_app_current_trip(void) {
    return s_trip;
}

static void set_debug_logging(bool debug) {
    static const char *const TAGS[] = { "yg_app", "yg_ble", "yg_imu", "yg_io", "yg_log", "yg_net", "yg_nvs" };
    for (size_t i = 0; i < sizeof(TAGS) / sizeof(TAGS[0]); i++) {
        esp_log_level_set(TAGS[i], debug ? ESP_LOG_DEBUG : ESP_LOG_INFO);
    }
}

void yg_app_setting_changed(yg_engine_t *e, uint32_t now, const char *key, const char *value) {
    yg_engine_setting_changed(e, now, key, value);
    if (strcmp(key, "debug") == 0) {
        set_debug_logging(e->s->debug);
    }
}

void yg_app_erase_everything(void) {
    /* Settings first, as erasing full logs takes a long time, and again after, as a
     * setting saved in the meantime writes them all back. */
    yg_nvs_factory_reset();
    yg_log_clear();
    yg_nvs_factory_reset();
}

void yg_app_settings_reset(yg_engine_t *e, uint32_t now) {
    char ssid[YG_SSID_MAX + 1];
    yg_nvs_default_ssid(ssid, sizeof(ssid));
    yg_engine_settings_reset(e, now, ssid);
    set_debug_logging(e->s->debug);
}

void yg_app_trips_restart(yg_engine_t *e) {
    yg_engine_trips_restart(e);
    s_trip = e->trip;
}

void yg_app_trips_restarted(yg_engine_t *e, uint32_t now) {
    yg_nvs_set_trip(e->trip);
    yg_engine_log_restarted(e, now);
}

/* The engine's connection to the hardware. */
static void op_press(void *ctx, uint32_t ms) { (void)ctx; yg_io_press(ms); }
static void op_led(void *ctx, yg_led_event_t e) { (void)ctx; yg_io_led(e); }
static void op_buzz(void *ctx, yg_buzz_event_t e) { (void)ctx; yg_io_buzz(e, s_settings.buzzer_type); }
static void op_log(void *ctx, const yg_record_t *r) { (void)ctx; yg_log_trip(r); }
static void op_capture(void *ctx, const yg_record_t *r) { (void)ctx; yg_log_capture(r); }
static void op_scan(void *ctx, yg_scan_mode_t mode, const yg_beacon_t *b, bool share_radio) { (void)ctx; yg_ble_scan(mode, b, share_radio); }
static void op_save_beacon(void *ctx, const yg_beacon_t *b) { (void)ctx; yg_nvs_save_beacon(b); }
static void op_save_settings(void *ctx) { (void)ctx; yg_nvs_save_settings(&s_settings); }

static void op_hotspot(void *ctx, bool on) {
    (void)ctx;
    if (s_hooks.hotspot != NULL) {
        s_hooks.hotspot(on);
    }
}

static void op_env_start(void *ctx) {
    (void)ctx;
    yg_ble_count_begin();
    if (s_hooks.env_start != NULL) {
        s_hooks.env_start();
    }
}

static void op_env_finish(void *ctx, yg_env_t *out) {
    (void)ctx;
    memset(out, 0, sizeof(*out));
    if (s_hooks.env_finish != NULL) {
        s_hooks.env_finish(out);
    }
    out->ble_devices = yg_ble_count_end();
}

/* Stays in the decision task on purpose, so that until the restart nothing can press the
 * remote or save a setting. Erasing takes too long for the watchdog, so it stops watching. */
static void op_factory_reset(void *ctx) {
    (void)ctx;
    const yg_sound_t *sound = yg_sound_get(YG_BUZZ_ERASE);
    uint32_t sound_ms = 0;
    for (uint8_t i = 0; i < sound->count; i++) {
        sound_ms += sound->steps[i];
    }
    esp_task_wdt_delete(NULL);
    /* A little over, because flash work holds up the task that turns the buzzer off. */
    vTaskDelay(pdMS_TO_TICKS(sound_ms + 100));
    yg_app_erase_everything();
    esp_restart();
}

static const yg_engine_ops_t OPS = {
    .press = op_press,
    .led = op_led,
    .buzz = op_buzz,
    .log = op_log,
    .capture = op_capture,
    .hotspot = op_hotspot,
    .scan = op_scan,
    .save_beacon = op_save_beacon,
    .save_settings = op_save_settings,
    .env_start = op_env_start,
    .env_finish = op_env_finish,
    .factory_reset = op_factory_reset,
};

static void check_new_firmware(uint32_t now) {
    if (!s_ota_pending) {
        return;
    }
    bool hotspot_ok = s_hooks.hotspot_running == NULL || s_hooks.hotspot_running();
    if (s_settings_ok && yg_ble_is_scanning() && hotspot_ok && !s_test_fail_self_check) {
        esp_ota_mark_app_valid_cancel_rollback();
        yg_engine_log_ota(&s_engine, now, YG_OTA_EVENT_CONFIRMED);
        ESP_LOGI(TAG, "New firmware passed its self-check");
        s_ota_pending = false;
    } else if (now - s_engine.boot_time >= OTA_DEADLINE_MS) {
        ESP_LOGE(TAG, "New firmware failed its self-check, rolling back");
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}

static void handle(const event_t *ev) {
    switch (ev->kind) {
    case EV_ADVERT:
        yg_engine_on_advert(&s_engine, ev->t, &ev->advert.addr, ev->advert.rssi, &ev->advert.ib);
        break;
    case EV_IMU:
        s_engine.sensor_id = yg_imu_id();
        yg_engine_on_imu(&s_engine, ev->t, &ev->imu.sample, ev->imu.temp_c);
        break;
    case EV_IMU_FAILED:
        yg_engine_on_imu_failed(&s_engine, ev->t, ev->absent);
        break;
    case EV_GESTURE:
        yg_engine_on_gesture(&s_engine, ev->t, ev->gesture);
        break;
    case EV_CLIENTS:
        yg_engine_on_clients(&s_engine, ev->t, ev->clients);
        break;
    case EV_CALL:
        ev->call.fn(&s_engine, now_ms(), ev->call.arg);
        xSemaphoreGive(ev->call.done);
        break;
    }
}

static void decision_task(void *arg) {
    (void)arg;
    esp_task_wdt_add(NULL);
    uint32_t last_tick = now_ms();
    uint32_t last_second = last_tick;
    for (;;) {
        event_t ev;
        if (xQueueReceive(s_queue, &ev, pdMS_TO_TICKS(50)) == pdTRUE) {
            handle(&ev);
        }
        uint32_t now = now_ms();
        if (now - last_tick >= TICK_MS) {
            last_tick = now;
            yg_engine_tick(&s_engine, now);
        }
        if (now - last_second >= 1000u) {
            last_second = now;
            yg_ble_ensure_scanning();
            check_new_firmware(now);
            uint32_t dropped = s_dropped;
            if (dropped != s_dropped_logged) {
                yg_engine_log_error(&s_engine, now, YG_ERROR_QUEUE_DROPPED, dropped);
                s_dropped_logged = dropped;
            }
        }
        esp_task_wdt_reset();
    }
}

void yg_app_start(const yg_app_hooks_t *hooks) {
    s_hooks = *hooks;
    s_queue = xQueueCreate(QUEUE_LENGTH, sizeof(event_t));

    s_settings_ok = yg_nvs_load(&s_settings, &s_beacon);
    set_debug_logging(s_settings.debug);
    s_trip = yg_nvs_next_trip();

    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK
        && state == ESP_OTA_IMG_PENDING_VERIFY) {
        s_ota_pending = true;
        s_test_fail_self_check = yg_nvs_take_test_flag();
    }

    bool sensor_found = yg_imu_init();

    yg_ble_start(on_advert);

    const yg_boot_info_t boot = {
        .trip = s_trip,
        .fw = esp_app_get_description()->version,
        .reset_reason = (uint8_t)esp_reset_reason(),
        .sensor_id = yg_imu_id(),
        .force_hotspot = s_ota_pending,
    };
    yg_engine_init(&s_engine, &OPS, &s_settings, &s_beacon, &boot, now_ms());

    yg_io_start(on_gesture);
    yg_imu_start(on_imu_sample, on_imu_failed);
    xTaskCreate(decision_task, "yg_app", 8192, NULL, 8, NULL);
    ESP_LOGI(TAG, "Trip %u, firmware %s, sensor %s", (unsigned)s_trip, boot.fw, sensor_found ? "found" : "not found yet");
}
