/* The brain: runs in one task, takes events, and talks to hardware only through
 * the ops table, so all of it can run on a PC. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "yg_capture.h"
#include "yg_gesture.h"
#include "yg_hotspot_policy.h"
#include "yg_ibeacon.h"
#include "yg_motion.h"
#include "yg_pairing.h"
#include "yg_record.h"
#include "yg_rfcode.h"
#include "yg_sensor_health.h"
#include "yg_settings.h"
#include "yg_sound.h"
#include "yg_trace.h"
#include "yg_trigger.h"
#include "yg_types.h"

typedef enum
{
    YG_LED_OFF,
    YG_LED_PRESS,          /* White during a press. */
    YG_LED_PAIRING,        /* Fast blue blink until the next event. */
    YG_LED_SAVED,          /* Green for 2 s. */
    YG_LED_REJECTED,       /* Three red blinks. */
    YG_LED_SENSOR_FAILED,  /* Red double blink every 10 s until the next event. */
    YG_LED_UNPAIRED,       /* Slow amber blink for 30 s. */
    YG_LED_HOTSPOT,        /* Cyan for 2 s. */
    YG_LED_ERASING,        /* Magenta until a factory reset restarts the unit. */
} yg_led_event_t;

typedef enum
{
    YG_SCAN_OFF,
    YG_SCAN_FILTERED,   /* Only the paired beacon reaches the code. */
    YG_SCAN_OPEN,       /* Everything, for pairing and the debug snapshot. */
} yg_scan_mode_t;

typedef struct
{
    void (*press)(void *ctx, uint32_t ms);
    void (*led)(void *ctx, yg_led_event_t e);
    void (*buzz)(void *ctx, yg_buzz_event_t e);
    void (*log)(void *ctx, const yg_record_t *r);
    void (*capture)(void *ctx, const yg_record_t *r);
    void (*hotspot)(void *ctx, bool on);
    /* share_radio: leave most of the shared radio to Wi-Fi, and hear fewer adverts for it. */
    void (*scan)(void *ctx, yg_scan_mode_t mode, const yg_beacon_t *beacon, bool share_radio);
    void (*save_beacon)(void *ctx, const yg_beacon_t *b);
    void (*save_settings)(void *ctx);
    void (*env_start)(void *ctx);
    void (*env_finish)(void *ctx, yg_env_t *out);
    /* Erases everything and restarts, so on the unit this never returns. */
    void (*factory_reset)(void *ctx);
    void (*save_gyro_zero)(void *ctx, const float zero[3]);
    /* RF variant only; left NULL on the MOSFET build. */
    void (*save_rf_code)(void *ctx, const yg_rfcode_t *code);
    void (*rf_listen)(void *ctx, bool on);
    void *ctx;
} yg_engine_ops_t;

typedef struct
{
    uint32_t trip;
    const char *fw;
    uint8_t reset_reason;
    uint8_t sensor_id;
    bool force_hotspot;
    bool has_gyro_zero;
    float gyro_zero[3];
    bool rf_learned;
} yg_boot_info_t;

#define YG_ENGINE_RECENT_ADVERTS 32

typedef enum
{
    YG_ENV_WAITING,
    YG_ENV_RUNNING,
    YG_ENV_DONE,
} yg_env_phase_t;

typedef struct
{
    const yg_engine_ops_t *ops;
    yg_settings_t *s;
    yg_beacon_t beacon;
    uint32_t trip;
    uint32_t boot_time;
    const char *fw;
    uint8_t sensor_id;
    uint8_t reset_reason;

    yg_trigger_t trigger;
    yg_motion_t motion;
    yg_motion_out_t motion_out;
    yg_imu_sample_t last_sample;
    yg_sensor_health_t health;
    yg_sensor_state_t health_seen;
    yg_pairing_t pairing;
    yg_trace_t trace;
    yg_hotspot_policy_t hotspot;
    yg_capture_t *capture;
    yg_scan_mode_t scan_mode;

    bool hotspot_on;
    uint8_t clients;
    bool heard_logged;
    bool has_advert;
    int8_t last_rssi;
    uint32_t last_advert;
    bool was_pressing;
    bool moved;
    bool motion_logged;
    uint32_t last_motion_log;
    bool near_miss_captured;
    uint32_t last_near_miss;
    bool has_temp;
    float temp_c;
    bool temp_logged;
    uint32_t last_temp_log;
    bool sensor_out;
    uint32_t sensor_out_since;
    bool glitch_logged;
    uint32_t last_glitch_log;
    bool sensor_sounded;
    uint16_t pair_saves;
    bool learning;
    uint32_t learn_started;
    bool rf_learned;
    uint32_t recent_time[YG_ENGINE_RECENT_ADVERTS];
    int8_t recent_rssi[YG_ENGINE_RECENT_ADVERTS];
    uint8_t recent_head;
    uint8_t recent_count;
    uint32_t second_start;
    uint16_t adverts_this_second;
    uint16_t adverts_per_second;
    uint32_t live_start;
    float live_mg, live_prev_mg;
    float live_yaw, live_prev_yaw;
    yg_env_phase_t env_phase;
    uint32_t env_started;
} yg_engine_t;

void yg_engine_init(yg_engine_t *e, const yg_engine_ops_t *ops, yg_settings_t *s, const yg_beacon_t *beacon,
                    const yg_boot_info_t *boot, uint32_t now);

void yg_engine_on_advert(yg_engine_t *e, uint32_t now, const yg_addr_t *a, int8_t rssi, const yg_ibeacon_t *ib);
void yg_engine_on_imu(yg_engine_t *e, uint32_t now, const yg_imu_sample_t *s, float temp_c);
void yg_engine_on_imu_failed(yg_engine_t *e, uint32_t now, bool absent);
void yg_engine_on_gesture(yg_engine_t *e, uint32_t now, yg_gesture_t g);
void yg_engine_on_clients(yg_engine_t *e, uint32_t now, uint8_t clients);
void yg_engine_tick(yg_engine_t *e, uint32_t now);

bool yg_engine_press(yg_engine_t *e, uint32_t now, yg_press_reason_t reason);
bool yg_engine_setpoint_here(yg_engine_t *e, uint32_t now);
bool yg_engine_setpoint_dbm(yg_engine_t *e, uint32_t now, int dbm);   /* YG_SET_POINT_ANY for "any". */
void yg_engine_pair_start(yg_engine_t *e, uint32_t now, yg_pair_mode_t mode);
void yg_engine_pair_cancel(yg_engine_t *e, uint32_t now);
void yg_engine_pair_set(yg_engine_t *e, uint32_t now, const uint8_t addr[YG_ADDR_LEN], uint8_t type);
void yg_engine_pair_forget(yg_engine_t *e, uint32_t now);
void yg_engine_hotspot_request(yg_engine_t *e, uint32_t now);
bool yg_engine_simulate_motion(yg_engine_t *e, uint32_t now);   /* Debug flag only. */
bool yg_engine_capture_now(yg_engine_t *e, uint32_t now);       /* Debug flag only. */

/* RF variant: learn the gate remote's fixed code, cancel, or forget it. Unused on the
 * MOSFET build. yg_engine_on_rf_capture feeds one captured burst to the learner. */
void yg_engine_rf_learn_start(yg_engine_t *e, uint32_t now);
void yg_engine_rf_learn_cancel(yg_engine_t *e, uint32_t now);
void yg_engine_rf_forget(yg_engine_t *e, uint32_t now);
void yg_engine_on_rf_capture(yg_engine_t *e, uint32_t now, const uint16_t *durations, uint16_t n, bool first_high);

/* Call after a setting changes. The value is logged, never for the password. */
void yg_engine_setting_changed(yg_engine_t *e, uint32_t now, const char *key, const char *value);

/* Puts every setting back to its default, keeping the beacon, logs and trip count. The
 * default hotspot name comes from the unit's address, so the caller supplies it. */
void yg_engine_settings_reset(yg_engine_t *e, uint32_t now, const char *default_ssid);

/* Reset logs. Call the first before erasing, so that whatever is logged during the erase
 * already counts as trip 1, and the second after it, to start this trip's log again. */
void yg_engine_trips_restart(yg_engine_t *e);
void yg_engine_log_restarted(yg_engine_t *e, uint32_t now);
bool yg_engine_rate_trip(yg_engine_t *e, uint32_t now, uint32_t trip, const char *rating);
void yg_engine_time_anchor(yg_engine_t *e, uint32_t now, uint32_t epoch_s);
void yg_engine_log_ota(yg_engine_t *e, uint32_t now, uint8_t event);
void yg_engine_log_error(yg_engine_t *e, uint32_t now, uint8_t code, uint32_t detail);

int yg_engine_status_json(const yg_engine_t *e, uint32_t now, char *buf, size_t len);
int yg_engine_debug_json(const yg_engine_t *e, uint32_t now, char *buf, size_t len);
