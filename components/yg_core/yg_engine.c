#include "yg_engine.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yg_json.h"

#define MOTION_LOG_GAP_MS    10000u
#define NEAR_MISS_GAP_MS     60000u
#define TEMP_LOG_GAP_MS      60000u
#define GLITCH_MIN_MS        100u
#define GLITCH_LOG_GAP_MS    10000u
#define SET_POINT_WINDOW_MS  3000u
#define SET_POINT_WINDOW_SHARED_MS 10000u    /* Far fewer adverts are heard while sharing the radio. */
#define LIVE_PEAK_WINDOW_MS  500u
#define ENV_DELAY_MS         10000u
#define ENV_RUN_MS           6000u    /* A Wi-Fi scan with the hotspot up takes a few seconds. */
#define CAPTURE_RECORDS_PER_TICK 3

static void led(yg_engine_t *e, yg_led_event_t ev) {
    if (e->s->led_enabled) {
        e->ops->led(e->ops->ctx, ev);
    }
}

static void buzz(yg_engine_t *e, yg_buzz_event_t ev) {
    if (e->s->buzzer_enabled) {
        e->ops->buzz(e->ops->ctx, ev);
    }
}

static void log_record(yg_engine_t *e, const yg_record_t *r) {
    e->ops->log(e->ops->ctx, r);
}

static void led_idle(yg_engine_t *e) {
    led(e, e->health.state == YG_SENSOR_FAILED ? YG_LED_SENSOR_FAILED : YG_LED_OFF);
}

static void start_scan(yg_engine_t *e, yg_scan_mode_t mode) {
    e->scan_mode = mode;
    e->ops->scan(e->ops->ctx, mode, &e->beacon, e->hotspot_on);
}

static void restore_scan(yg_engine_t *e) {
    yg_scan_mode_t mode = YG_SCAN_OFF;
    if (e->pairing.mode != YG_PAIR_IDLE) {
        mode = YG_SCAN_OPEN;
    } else if (e->beacon.paired) {
        mode = YG_SCAN_FILTERED;
    }
    start_scan(e, mode);
}

static void end_env_scan_quietly(yg_engine_t *e) {
    if (e->env_phase == YG_ENV_RUNNING) {
        yg_env_t ignored;
        e->ops->env_finish(e->ops->ctx, &ignored);
        e->env_phase = YG_ENV_DONE;
        restore_scan(e);
    }
}

static void start_press(yg_engine_t *e, uint32_t now, yg_press_reason_t reason, int8_t rssi) {
    yg_record_t r;
    uint32_t since_motion = e->trigger.has_motion ? now - e->trigger.last_motion : 0;
    e->ops->press(e->ops->ctx, e->s->press_ms);
    e->was_pressing = true;
    yg_rec_press(&r, e->trip, now, (uint8_t)reason, rssi, since_motion, yg_trigger_effective_set_point(&e->trigger, e->s));
    log_record(e, &r);
    led(e, YG_LED_PRESS);
    /* It only answers a person. The gate opening by itself on every trip would get old. */
    if (reason == YG_PRESS_BUTTON || reason == YG_PRESS_WEB) {
        buzz(e, YG_BUZZ_PRESS);
    }
    if (e->capture != NULL) {
        yg_capture_trigger(e->capture, now, e->trip, YG_CAP_CAUSE_PRESS);
    }
}

static void on_motion(yg_engine_t *e, uint32_t now, uint8_t cause, float mg, float yaw) {
    yg_trigger_on_motion(&e->trigger, now);
    yg_trace_on_motion(&e->trace, now);
    yg_hotspot_on_motion(&e->hotspot);
    e->moved = true;
    end_env_scan_quietly(e);
    if (!e->motion_logged || now - e->last_motion_log >= MOTION_LOG_GAP_MS) {
        yg_record_t r;
        yg_rec_motion(&r, e->trip, now, cause, (uint16_t)(mg + 0.5f), yaw);
        log_record(e, &r);
        e->motion_logged = true;
        e->last_motion_log = now;
        /* Only where the trace is sampling too. An hour's drive would otherwise overwrite
         * every capture of pulling out and of reaching the gate. */
        if (e->capture != NULL && yg_trace_is_sampling(&e->trace, now)) {
            yg_capture_trigger(e->capture, now, e->trip, YG_CAP_CAUSE_MOTION);
        }
    }
}

static void sync_sensor(yg_engine_t *e, uint32_t now) {
    yg_sensor_state_t state = e->health.state;
    if (state == e->health_seen) {
        return;
    }
    yg_record_t r;
    if (state == YG_SENSOR_FAILED) {
        static const uint8_t CODES[] = {
            [YG_SENSOR_ERR_ABSENT] = YG_ERROR_SENSOR_ABSENT,
            [YG_SENSOR_ERR_GRAVITY] = YG_ERROR_SENSOR_GRAVITY,
            [YG_SENSOR_ERR_READS] = YG_ERROR_SENSOR_READS,
            [YG_SENSOR_ERR_STUCK] = YG_ERROR_SENSOR_STUCK,
        };
        yg_trigger_set_sensor_ok(&e->trigger, false);
        yg_rec_error(&r, e->trip, now, CODES[e->health.err], e->sensor_id);
        log_record(e, &r);
        led(e, YG_LED_SENSOR_FAILED);
        /* The LED is hidden in the case, and this can open the gate from the parking spot,
         * so it is worth one sound. A sensor that keeps dropping out gets no more than that. */
        if (e->s->motion_enabled && !e->sensor_sounded) {
            e->sensor_sounded = true;
            buzz(e, YG_BUZZ_SENSOR);
        }
    } else if (e->health_seen == YG_SENSOR_FAILED) {
        yg_trigger_set_sensor_ok(&e->trigger, true);
        yg_rec_error(&r, e->trip, now, YG_ERROR_SENSOR_RECOVERED, 0);
        log_record(e, &r);
        led_idle(e);
    }
    e->health_seen = state;
}

static void update_capture_allocation(yg_engine_t *e) {
    if (e->s->debug && e->capture == NULL) {
        e->capture = malloc(sizeof(yg_capture_t));
        if (e->capture != NULL) {
            yg_capture_init(e->capture);
        }
    } else if (!e->s->debug && e->capture != NULL) {
        free(e->capture);
        e->capture = NULL;
    }
}

void yg_engine_init(yg_engine_t *e, const yg_engine_ops_t *ops, yg_settings_t *s, const yg_beacon_t *beacon,
                    const yg_boot_info_t *boot, uint32_t now) {
    if (e->capture != NULL) {
        free(e->capture);
    }
    memset(e, 0, sizeof(*e));
    e->ops = ops;
    e->s = s;
    e->beacon = *beacon;
    e->trip = boot->trip;
    e->boot_time = now;
    e->fw = boot->fw;
    e->sensor_id = boot->sensor_id;
    e->reset_reason = boot->reset_reason;
    e->rf_learned = boot->rf_learned;
    e->second_start = now;

    yg_trigger_init(&e->trigger, beacon->paired);
    yg_motion_init(&e->motion);
    if (boot->has_gyro_zero) {
        yg_motion_set_known_zero(&e->motion, boot->gyro_zero);
    }
    yg_sensor_health_init(&e->health, now);
    e->health_seen = e->health.state;
    yg_pairing_init(&e->pairing);
    yg_trace_init(&e->trace, now);
    yg_hotspot_init(&e->hotspot, s, now, boot->force_hotspot);
    update_capture_allocation(e);

    yg_record_t r;
    yg_rec_boot(&r, e->trip, now, boot->fw, boot->reset_reason, boot->sensor_id, beacon, s);
    log_record(e, &r);
    restore_scan(e);
    if (!beacon->paired) {
        led(e, YG_LED_UNPAIRED);
        buzz(e, YG_BUZZ_NO_BEACON);
    }
}

void yg_engine_on_advert(yg_engine_t *e, uint32_t now, const yg_addr_t *a, int8_t rssi, const yg_ibeacon_t *ib) {
    yg_pairing_on_advert(&e->pairing, now, a, rssi, ib);
    if (!e->beacon.paired || !yg_addr_equal(a, &e->beacon.addr)) {
        return;
    }
    e->has_advert = true;
    e->last_rssi = rssi;
    e->last_advert = now;
    e->recent_time[e->recent_head] = now;
    e->recent_rssi[e->recent_head] = rssi;
    e->recent_head = (uint8_t)((e->recent_head + 1) % YG_ENGINE_RECENT_ADVERTS);
    if (e->recent_count < YG_ENGINE_RECENT_ADVERTS) {
        e->recent_count++;
    }
    e->adverts_this_second++;
    if (!e->heard_logged) {
        yg_record_t r;
        yg_rec_first_heard(&r, e->trip, now, rssi);
        log_record(e, &r);
        e->heard_logged = true;
    }
    yg_trace_on_advert(&e->trace, now, rssi);
    if (e->capture != NULL) {
        yg_capture_on_advert(e->capture, now, rssi);
    }
    yg_press_reason_t reason = yg_trigger_on_advert(&e->trigger, e->s, now, rssi);
    if (reason != YG_PRESS_NONE) {
        start_press(e, now, reason, rssi);
    }
}

/* The page reads the status twice a second, far slower than the sensor, so it gets the
 * strongest values of the last half second or more, however many pages are asking. */
static void note_live_motion(yg_engine_t *e, uint32_t now, const yg_motion_out_t *out) {
    if (now - e->live_start >= LIVE_PEAK_WINDOW_MS) {
        e->live_prev_mg = e->live_mg;
        e->live_prev_yaw = e->live_yaw;
        e->live_mg = 0;
        e->live_yaw = 0;
        e->live_start = now;
    }
    e->live_mg = fmaxf(e->live_mg, out->horizontal_mg);
    e->live_yaw = fmaxf(e->live_yaw, fabsf(out->yaw_sum_deg));
}

static void note_gyro_zero(yg_engine_t *e, uint32_t now) {
    yg_zero_event_t zero;
    if (!yg_motion_take_zero_event(&e->motion, &zero)) {
        return;
    }
    if (zero.keep) {
        e->ops->save_gyro_zero(e->ops->ctx, zero.zero);
    }
    yg_record_t r;
    yg_rec_gyro_zero(&r, e->trip, now, (uint8_t)zero.source, zero.keep, zero.zero);
    log_record(e, &r);
}

/* An outage too short to count as a failure can still explain false motion in a log. */
static void note_sensor_back(yg_engine_t *e, uint32_t now) {
    if (!e->sensor_out) {
        return;
    }
    e->sensor_out = false;
    uint32_t lasted = now - e->sensor_out_since;
    if (lasted < GLITCH_MIN_MS || e->health.state == YG_SENSOR_FAILED
        || (e->glitch_logged && now - e->last_glitch_log < GLITCH_LOG_GAP_MS)) {
        return;
    }
    yg_record_t r;
    yg_rec_error(&r, e->trip, now, YG_ERROR_SENSOR_GLITCH, lasted);
    log_record(e, &r);
    e->glitch_logged = true;
    e->last_glitch_log = now;
}

void yg_engine_on_imu(yg_engine_t *e, uint32_t now, const yg_imu_sample_t *s, float temp_c) {
    /* A sensor reset by a power glitch sleeps, and reads as all zeros, which never happens
     * for real because of gravity. */
    if (s->ax == 0 && s->ay == 0 && s->az == 0 && s->gx == 0 && s->gy == 0 && s->gz == 0) {
        yg_engine_on_imu_failed(e, now, false);
        return;
    }
    note_sensor_back(e, now);
    yg_sensor_health_on_sample(&e->health, now, s);
    sync_sensor(e, now);
    e->last_sample = *s;
    e->has_temp = true;
    e->temp_c = temp_c;
    if (e->capture != NULL) {
        yg_capture_on_imu(e->capture, s);
    }
    if (e->health.state == YG_SENSOR_FAILED) {
        return;
    }
    yg_motion_out_t *out = &e->motion_out;
    yg_motion_update(&e->motion, e->s, now, s, out);
    note_gyro_zero(e, now);
    note_live_motion(e, now, out);
    if (out->cause != YG_MOTION_NONE) {
        on_motion(e, now, (uint8_t)out->cause, out->horizontal_mg, out->yaw_sum_deg);
    }
    if (out->tilting) {
        yg_trace_on_flags(&e->trace, YG_TRACE_FLAG_TILTING);
    }
    if (out->near_miss) {
        yg_trace_on_flags(&e->trace, YG_TRACE_FLAG_NEAR_MISS);
        if (e->capture != NULL && out->cause == YG_MOTION_NONE && yg_trace_is_sampling(&e->trace, now)
            && (!e->near_miss_captured || now - e->last_near_miss >= NEAR_MISS_GAP_MS)
            && yg_capture_trigger(e->capture, now, e->trip, YG_CAP_CAUSE_NEAR_MISS)) {
            e->near_miss_captured = true;
            e->last_near_miss = now;
        }
    }
}

void yg_engine_on_imu_failed(yg_engine_t *e, uint32_t now, bool absent) {
    if (!e->sensor_out) {
        e->sensor_out = true;
        e->sensor_out_since = now;
    }
    yg_sensor_health_on_read_failed(&e->health, now, absent);
    sync_sensor(e, now);
}

void yg_engine_on_gesture(yg_engine_t *e, uint32_t now, yg_gesture_t g) {
    switch (g) {
    case YG_GESTURE_SINGLE:
        yg_engine_press(e, now, YG_PRESS_BUTTON);
        break;
    case YG_GESTURE_DOUBLE:
        yg_engine_setpoint_here(e, now);
        break;
    case YG_GESTURE_TRIPLE:
        yg_engine_hotspot_request(e, now);
        break;
    case YG_GESTURE_HOLD:
        yg_engine_pair_start(e, now, YG_PAIR_AUTO);
        break;
    case YG_GESTURE_LEARN:
        /* RF variant only: rf_listen is wired only on that build. Elsewhere the gesture
         * is refused out loud, as the buzzer answers whoever is at the button. */
        if (e->ops->rf_listen != NULL) {
            yg_engine_rf_learn_start(e, now);
        } else {
            led(e, YG_LED_REJECTED);
            buzz(e, YG_BUZZ_REJECTED);
        }
        break;
    case YG_GESTURE_FACTORY_RESET:
        led(e, YG_LED_ERASING);
        buzz(e, YG_BUZZ_ERASE);
        e->ops->factory_reset(e->ops->ctx);
        break;
    default:
        break;
    }
}

void yg_engine_on_clients(yg_engine_t *e, uint32_t now, uint8_t clients) {
    yg_hotspot_set_clients(&e->hotspot, now, clients);
    if (clients != e->clients) {
        yg_record_t r;
        e->clients = clients;
        yg_rec_hotspot(&r, e->trip, now, e->hotspot_on, clients);
        log_record(e, &r);
    }
}

static void tick_pairing(yg_engine_t *e, uint32_t now) {
    yg_pair_mode_t mode = e->pairing.mode;
    yg_addr_t winner;
    yg_pair_result_t result = yg_pairing_tick(&e->pairing, e->s, now, &winner);
    if (result == YG_PAIR_RESULT_SAVED) {
        yg_engine_pair_set(e, now, winner.addr, winner.type);
    } else if (result == YG_PAIR_RESULT_TIMEOUT) {
        yg_record_t r;
        yg_trigger_set_suspended(&e->trigger, false);
        restore_scan(e);
        yg_rec_pair(&r, e->trip, now, YG_PAIR_EVENT_TIMEOUT, NULL);
        log_record(e, &r);
        if (mode == YG_PAIR_AUTO) {
            led(e, YG_LED_REJECTED);
            buzz(e, YG_BUZZ_REJECTED);
        } else {
            led_idle(e);
        }
    }
}

static void tick_env(yg_engine_t *e, uint32_t now) {
    if (e->env_phase == YG_ENV_WAITING && e->s->debug && !e->moved && e->pairing.mode == YG_PAIR_IDLE
        && now - e->boot_time >= ENV_DELAY_MS) {
        e->env_phase = YG_ENV_RUNNING;
        e->env_started = now;
        start_scan(e, YG_SCAN_OPEN);
        e->ops->env_start(e->ops->ctx);
    } else if (e->env_phase == YG_ENV_RUNNING && now - e->env_started >= ENV_RUN_MS) {
        yg_env_t env;
        yg_record_t r;
        e->ops->env_finish(e->ops->ctx, &env);
        e->env_phase = YG_ENV_DONE;
        restore_scan(e);
        yg_rec_env(&r, e->trip, now, &env);
        log_record(e, &r);
    }
}

void yg_engine_tick(yg_engine_t *e, uint32_t now) {
    yg_record_t r;

    yg_rearm_reason_t rearm = yg_trigger_tick(&e->trigger, e->s, now);
    if (e->was_pressing && e->trigger.state != YG_TRIG_PRESSING) {
        e->was_pressing = false;
        led_idle(e);
    }
    if (rearm != YG_REARM_NONE) {
        yg_rec_rearm(&r, e->trip, now, (uint8_t)rearm);
        log_record(e, &r);
    }

    tick_pairing(e, now);

    if (e->learning && now - e->learn_started >= (uint32_t)e->s->pair_timeout_s * 1000u) {
        e->learning = false;
        if (e->ops->rf_listen != NULL) {
            e->ops->rf_listen(e->ops->ctx, false);
        }
        led(e, YG_LED_REJECTED);
        buzz(e, YG_BUZZ_REJECTED);
    }

    uint16_t accel_mg, yaw_dps;
    yg_motion_take_peaks(&e->motion, &accel_mg, &yaw_dps);
    yg_trace_on_peaks(&e->trace, accel_mg, yaw_dps);
    if (yg_trace_tick(&e->trace, now, e->trip, &r)) {
        log_record(e, &r);
    }

    bool want_hotspot = yg_hotspot_tick(&e->hotspot, e->s, now);
    if (want_hotspot != e->hotspot_on) {
        e->hotspot_on = want_hotspot;
        /* Wi-Fi and the scan share one radio, and a scan at full time leaves Wi-Fi nothing. */
        start_scan(e, e->scan_mode);
        e->ops->hotspot(e->ops->ctx, want_hotspot);
        yg_rec_hotspot(&r, e->trip, now, want_hotspot, e->clients);
        log_record(e, &r);
    }

    tick_env(e, now);

    if (e->capture != NULL) {
        for (int i = 0; i < CAPTURE_RECORDS_PER_TICK && yg_capture_next(e->capture, now, e->trip, &r); i++) {
            e->ops->capture(e->ops->ctx, &r);
        }
    }

    if (e->has_temp && (!e->temp_logged || now - e->last_temp_log >= TEMP_LOG_GAP_MS)) {
        yg_rec_temp(&r, e->trip, now, e->temp_c);
        log_record(e, &r);
        e->temp_logged = true;
        e->last_temp_log = now;
    }

    if (now - e->second_start >= 1000u) {
        e->adverts_per_second = e->adverts_this_second;
        e->adverts_this_second = 0;
        e->second_start = now;
    }
}

bool yg_engine_remote_ready(const yg_engine_t *e) {
    /* rf_listen is wired only on the RF build, which has nothing to send until a remote is learned. */
    return e->ops->rf_listen == NULL || e->rf_learned;
}

bool yg_engine_press(yg_engine_t *e, uint32_t now, yg_press_reason_t reason) {
    /* With nothing to send, the press sound would tell the person it worked. */
    if (!yg_engine_remote_ready(e)) {
        led(e, YG_LED_REJECTED);
        buzz(e, YG_BUZZ_REJECTED);
        return false;
    }
    yg_press_reason_t started = yg_trigger_manual(&e->trigger, now, reason);
    if (started == YG_PRESS_NONE) {
        return false;
    }
    bool recent = e->has_advert && now - e->last_advert <= SET_POINT_WINDOW_MS;
    start_press(e, now, started, recent ? e->last_rssi : 0);
    return true;
}

bool yg_engine_setpoint_here(yg_engine_t *e, uint32_t now) {
    bool found = false;
    int8_t best = 0;
    uint32_t window = e->hotspot_on ? SET_POINT_WINDOW_SHARED_MS : SET_POINT_WINDOW_MS;
    for (uint8_t i = 0; i < e->recent_count; i++) {
        if (now - e->recent_time[i] <= window && (!found || e->recent_rssi[i] > best)) {
            best = e->recent_rssi[i];
            found = true;
        }
    }
    if (!found) {
        led(e, YG_LED_REJECTED);
        buzz(e, YG_BUZZ_REJECTED);
        return false;
    }
    if (best > -30) best = -30;
    if (best < -100) best = -100;
    yg_engine_setpoint_dbm(e, now, best);
    led(e, YG_LED_SAVED);
    buzz(e, YG_BUZZ_SAVED);
    return true;
}

bool yg_engine_setpoint_dbm(yg_engine_t *e, uint32_t now, int dbm) {
    if (!yg_settings_set_int(e->s, "set_point_dbm", dbm)) {
        return false;
    }
    char value[16];
    if (dbm == YG_SET_POINT_ANY) {
        strcpy(value, "any");
    } else {
        snprintf(value, sizeof(value), "%d", dbm);
    }
    yg_engine_setting_changed(e, now, "set_point_dbm", value);
    return true;
}

void yg_engine_pair_start(yg_engine_t *e, uint32_t now, yg_pair_mode_t mode) {
    end_env_scan_quietly(e);
    yg_pairing_start(&e->pairing, mode, now);
    yg_trigger_set_suspended(&e->trigger, true);
    restore_scan(e);
    led(e, YG_LED_PAIRING);
    if (mode == YG_PAIR_AUTO) {
        buzz(e, YG_BUZZ_PAIRING);
    }
}

void yg_engine_pair_cancel(yg_engine_t *e, uint32_t now) {
    (void)now;
    yg_pairing_cancel(&e->pairing);
    yg_trigger_set_suspended(&e->trigger, false);
    restore_scan(e);
    led_idle(e);
}

void yg_engine_pair_set(yg_engine_t *e, uint32_t now, const uint8_t addr[YG_ADDR_LEN], uint8_t type) {
    yg_record_t r;
    end_env_scan_quietly(e);
    yg_pairing_cancel(&e->pairing);
    e->beacon.paired = true;
    memcpy(e->beacon.addr.addr, addr, YG_ADDR_LEN);
    e->beacon.addr.type = type;
    e->pair_saves++;
    e->heard_logged = false;
    e->has_advert = false;
    e->recent_head = 0;
    e->recent_count = 0;
    e->ops->save_beacon(e->ops->ctx, &e->beacon);
    e->s->set_point_dbm = YG_SET_POINT_ANY;
    e->ops->save_settings(e->ops->ctx);
    yg_trigger_set_suspended(&e->trigger, false);
    yg_trigger_set_paired(&e->trigger, true);
    restore_scan(e);
    yg_rec_pair(&r, e->trip, now, YG_PAIR_EVENT_SAVED, &e->beacon.addr);
    log_record(e, &r);
    led(e, YG_LED_SAVED);
    buzz(e, YG_BUZZ_SAVED);
}

void yg_engine_pair_forget(yg_engine_t *e, uint32_t now) {
    yg_record_t r;
    yg_rec_pair(&r, e->trip, now, YG_PAIR_EVENT_FORGET, &e->beacon.addr);
    log_record(e, &r);
    end_env_scan_quietly(e);
    yg_pairing_cancel(&e->pairing);
    e->beacon.paired = false;
    e->has_advert = false;
    e->recent_head = 0;
    e->recent_count = 0;
    e->ops->save_beacon(e->ops->ctx, &e->beacon);
    e->s->set_point_dbm = YG_SET_POINT_ANY;
    e->ops->save_settings(e->ops->ctx);
    yg_trigger_set_suspended(&e->trigger, false);
    yg_trigger_set_paired(&e->trigger, false);
    restore_scan(e);
    led_idle(e);
}

void yg_engine_hotspot_request(yg_engine_t *e, uint32_t now) {
    yg_hotspot_request(&e->hotspot, now);
    led(e, YG_LED_HOTSPOT);
    buzz(e, YG_BUZZ_HOTSPOT);
}

bool yg_engine_simulate_motion(yg_engine_t *e, uint32_t now) {
    if (!e->s->debug) {
        return false;
    }
    on_motion(e, now, YG_MOTION_ACCEL, 0.0f, 0.0f);
    return true;
}

bool yg_engine_capture_now(yg_engine_t *e, uint32_t now) {
    if (!e->s->debug || e->capture == NULL) {
        return false;
    }
    return yg_capture_trigger(e->capture, now, e->trip, YG_CAP_CAUSE_MANUAL);
}

static void log_setting(yg_engine_t *e, uint32_t now, const char *key, const char *value) {
    yg_record_t r;
    bool secret = strcmp(key, "hotspot_password") == 0;
    yg_rec_setting(&r, e->trip, now, key, secret ? "changed" : value);
    log_record(e, &r);
}

void yg_engine_setting_changed(yg_engine_t *e, uint32_t now, const char *key, const char *value) {
    log_setting(e, now, key, value);
    e->ops->save_settings(e->ops->ctx);
    if (strcmp(key, "debug") == 0) {
        update_capture_allocation(e);
    } else if (strcmp(key, "led_enabled") == 0) {
        e->ops->led(e->ops->ctx, YG_LED_OFF);
        led_idle(e);
    }
}

void yg_engine_settings_reset(yg_engine_t *e, uint32_t now, const char *default_ssid) {
    static yg_settings_t old;
    old = *e->s;
    yg_settings_defaults(e->s);
    snprintf(e->s->hotspot_ssid, sizeof(e->s->hotspot_ssid), "%s", default_ssid);

    /* As ordinary setting records, so that a log replay follows the reset. */
    size_t count = 0;
    const yg_setting_desc_t *table = yg_settings_table(&count);
    for (size_t i = 0; i < count; i++) {
        char before[YG_PASSWORD_MAX + 1], after[YG_PASSWORD_MAX + 1];
        yg_settings_format(&old, &table[i], before, sizeof(before));
        yg_settings_format(e->s, &table[i], after, sizeof(after));
        if (strcmp(before, after) != 0) {
            log_setting(e, now, table[i].key, after);
        }
    }
    e->ops->save_settings(e->ops->ctx);
    update_capture_allocation(e);
    led(e, YG_LED_SAVED);
    buzz(e, YG_BUZZ_SAVED);
}

void yg_engine_trips_restart(yg_engine_t *e) {
    e->trip = 1;
    yg_trace_set_trip(&e->trace, e->trip);
}

void yg_engine_log_restarted(yg_engine_t *e, uint32_t now) {
    yg_record_t r;
    e->heard_logged = false;
    e->motion_logged = false;
    e->temp_logged = false;
    yg_rec_boot(&r, e->trip, now, e->fw, e->reset_reason, e->sensor_id, &e->beacon, e->s);
    log_record(e, &r);
}

bool yg_engine_rate_trip(yg_engine_t *e, uint32_t now, uint32_t trip, const char *rating) {
    uint8_t value = yg_rating_parse(rating);
    if (value == 0) {
        return false;
    }
    yg_record_t r;
    yg_rec_rating(&r, e->trip, now, trip, value);
    log_record(e, &r);
    return true;
}

void yg_engine_time_anchor(yg_engine_t *e, uint32_t now, uint32_t epoch_s) {
    yg_record_t r;
    yg_rec_time_anchor(&r, e->trip, now, epoch_s);
    log_record(e, &r);
}

void yg_engine_log_ota(yg_engine_t *e, uint32_t now, uint8_t event) {
    yg_record_t r;
    yg_rec_ota(&r, e->trip, now, event);
    log_record(e, &r);
}

void yg_engine_log_error(yg_engine_t *e, uint32_t now, uint8_t code, uint32_t detail) {
    yg_record_t r;
    yg_rec_error(&r, e->trip, now, code, detail);
    log_record(e, &r);
}

static const char *state_name(yg_trig_state_t state) {
    switch (state) {
    case YG_TRIG_UNPAIRED: return "unpaired";
    case YG_TRIG_ARMED: return "armed";
    case YG_TRIG_PRESSING: return "pressing";
    default: return "hold";
    }
}

static void json_set_point(yg_jw_t *w, int8_t value) {
    if (value == YG_SET_POINT_ANY) {
        yg_jw_raw(w, "\"any\"");
    } else {
        yg_jw_raw(w, "%d", (int)value);
    }
}

int yg_engine_status_json(const yg_engine_t *e, uint32_t now, char *buf, size_t len) {
    static const char *const SENSOR[] = { "starting", "ok", "failed" };
    static const char *const SENSOR_ERR[] = { "none", "absent", "gravity", "reads", "stuck" };
    static const char *const PAIR[] = { "idle", "auto", "list" };
    char mac[18];
    yg_jw_t w;
    yg_jw_init(&w, buf, len);
    yg_jw_raw(&w, "{\"fw\":");
    yg_jw_string(&w, e->fw);
    yg_jw_raw(&w, ",\"trip\":%u,\"uptime_ms\":%u,\"state\":\"%s\",\"paired\":%s,\"beacon\":",
              (unsigned)e->trip, (unsigned)(now - e->boot_time), state_name(e->trigger.state), e->beacon.paired ? "true" : "false");
    if (e->beacon.paired) {
        yg_addr_format(e->beacon.addr.addr, mac);
        yg_jw_raw(&w, "\"%s\"", mac);
    } else {
        yg_jw_raw(&w, "null");
    }
    yg_jw_raw(&w, ",\"set_point\":");
    json_set_point(&w, e->s->set_point_dbm);
    yg_jw_raw(&w, ",\"effective_set_point\":");
    json_set_point(&w, yg_trigger_effective_set_point(&e->trigger, e->s));
    yg_jw_raw(&w, ",\"rssi\":");
    if (e->has_advert) {
        yg_jw_raw(&w, "%d,\"rssi_age_ms\":%u", (int)e->last_rssi, (unsigned)(now - e->last_advert));
    } else {
        yg_jw_raw(&w, "null,\"rssi_age_ms\":null");
    }
    yg_jw_raw(&w, ",\"adverts_per_s\":%u,\"moved_recently\":%s,\"calibrated\":%s,\"tilting\":%s,\"motion_mg\":%d,\"motion_yaw\":",
              e->adverts_per_second, yg_trigger_moved_recently(&e->trigger, e->s, now) ? "true" : "false",
              e->motion_out.calibrated ? "true" : "false", e->motion_out.tilting ? "true" : "false", (int)(e->motion_out.horizontal_mg + 0.5f));
    yg_jw_tenths(&w, (int)(e->motion_out.yaw_sum_deg * 10.0f));
    yg_jw_raw(&w, ",\"motion_mg_peak\":%d,\"motion_yaw_peak\":", (int)(fmaxf(e->live_mg, e->live_prev_mg) + 0.5f));
    yg_jw_tenths(&w, (int)(fmaxf(e->live_yaw, e->live_prev_yaw) * 10.0f));
    yg_jw_raw(&w, ",\"motion_accel_mg\":%u,\"motion_yaw_deg\":%u", e->s->motion_accel_mg, e->s->motion_yaw_deg);
    yg_jw_raw(&w, ",\"sensor\":\"%s\",\"sensor_err\":\"%s\",\"sensor_id\":%u,\"temp_c\":",
              SENSOR[e->health.state], SENSOR_ERR[e->health.err], e->sensor_id);
    if (e->has_temp) {
        yg_jw_tenths(&w, (int)(e->temp_c * 10.0f));
    } else {
        yg_jw_raw(&w, "null");
    }
    yg_jw_raw(&w, ",\"hotspot_on\":%s,\"hotspot_left_s\":%u,\"clients\":%u,\"pair_mode\":\"%s\",\"pair_saves\":%u,\"debug\":%s,\"fallback\":%s,\"capturing\":%s,\"rf_learned\":%s,\"learning\":%s}",
              e->hotspot_on ? "true" : "false", (unsigned)yg_hotspot_left_s(&e->hotspot, e->s, now), e->clients,
              PAIR[e->pairing.mode], e->pair_saves, e->s->debug ? "true" : "false",
              yg_trigger_fallback_active(&e->trigger, e->s) ? "true" : "false",
              e->capture != NULL && yg_capture_active(e->capture) ? "true" : "false",
              e->rf_learned ? "true" : "false", e->learning ? "true" : "false");
    return yg_jw_finish(&w);
}

static void json_tenths_list(yg_jw_t *w, const float v[3], float scale) {
    yg_jw_raw(w, "[");
    for (int i = 0; i < 3; i++) {
        yg_jw_raw(w, "%s", i ? "," : "");
        yg_jw_tenths(w, (int)(v[i] * scale));
    }
    yg_jw_raw(w, "]");
}

int yg_engine_debug_json(const yg_engine_t *e, uint32_t now, char *buf, size_t len) {
    const yg_imu_sample_t *in = &e->last_sample;
    yg_jw_t w;
    yg_jw_init(&w, buf, len);
    yg_jw_raw(&w, "{\"uptime_ms\":%u,\"ax_mg\":%d,\"ay_mg\":%d,\"az_mg\":%d,\"gx\":", (unsigned)(now - e->boot_time),
              (int)lroundf(in->ax * 1000.0f), (int)lroundf(in->ay * 1000.0f), (int)lroundf(in->az * 1000.0f));
    yg_jw_tenths(&w, (int)(in->gx * 10.0f));
    yg_jw_raw(&w, ",\"gy\":");
    yg_jw_tenths(&w, (int)(in->gy * 10.0f));
    yg_jw_raw(&w, ",\"gz\":");
    yg_jw_tenths(&w, (int)(in->gz * 10.0f));
    yg_jw_raw(&w, ",\"gravity\":");
    json_tenths_list(&w, e->motion.gravity, 1000.0f);
    yg_jw_raw(&w, ",\"bias\":");
    json_tenths_list(&w, e->motion.bias, 10.0f);
    yg_jw_raw(&w, ",\"near_miss\":%s,\"scan\":%d}",
              e->motion_out.near_miss ? "true" : "false", (int)e->scan_mode);
    return yg_jw_finish(&w);
}

#define RF_LEARN_REPEATS 3u

void yg_engine_rf_learn_start(yg_engine_t *e, uint32_t now) {
    e->learning = true;
    e->learn_rejected = false;
    e->learn_started = now;
    if (e->ops->rf_listen != NULL) {
        e->ops->rf_listen(e->ops->ctx, true);
    }
    led(e, YG_LED_PAIRING);
    buzz(e, YG_BUZZ_PAIRING);
}

void yg_engine_on_rf_capture(yg_engine_t *e, uint32_t now, const uint16_t *durations, uint16_t n, bool first_high) {
    (void)now;
    if (!e->learning) {
        return;
    }
    yg_rfcode_t code = {0};
    if (yg_rfcode_learn(durations, n, first_high, RF_LEARN_REPEATS, &code)) {
        e->learning = false;
        e->rf_learned = true;
        if (e->ops->rf_listen != NULL) {
            e->ops->rf_listen(e->ops->ctx, false);
        }
        if (e->ops->save_rf_code != NULL) {
            e->ops->save_rf_code(e->ops->ctx, &code);
        }
        led(e, YG_LED_SAVED);
        buzz(e, YG_BUZZ_SAVED);
    } else if (!e->learn_rejected) {
        /* Once per session. A busy 433 MHz band can hand over burst after burst that is
         * not a remote, and a cue for each would never stop. Learning keeps listening. */
        e->learn_rejected = true;
        led(e, YG_LED_REJECTED);
        buzz(e, YG_BUZZ_REJECTED);
    }
}

void yg_engine_rf_forget(yg_engine_t *e, uint32_t now) {
    (void)now;
    e->rf_learned = false;
    yg_rfcode_t none = {0};
    if (e->ops->save_rf_code != NULL) {
        e->ops->save_rf_code(e->ops->ctx, &none);
    }
    led(e, YG_LED_SAVED);
    buzz(e, YG_BUZZ_SAVED);
}
