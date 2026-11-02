/* Replays recorded data through the real logic on a PC, to see what a change of
 * settings would have done. tools/yg_logs.py writes the input from a log file.
 *
 * Input, one per line, times in ms:
 *   set,<key>,<value>           change a setting before the replay starts
 *   A,<t>,<rssi>                advert from the paired beacon
 *   M,<t>                       motion, for traces that hold no raw samples
 *   I,<t>,ax,ay,az,gx,gy,gz     raw motion sample in g and degrees per second
 *   F,<t> and R,<t>             the log says the sensor failed, or recovered
 *   T,<t>                       time passes, runs the 100 ms tick
 *
 * Output: PRESS,<t>,<reason> and REARM,<t>,<reason> and MOTION,<t>,<cause>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yg_engine.h"

static yg_engine_t engine;
static yg_settings_t settings;
static bool started;

static void op_press(void *ctx, uint32_t ms) { (void)ctx; (void)ms; }
static void op_led(void *ctx, yg_led_event_t e) { (void)ctx; (void)e; }
static void op_buzz(void *ctx, yg_buzz_event_t e) { (void)ctx; (void)e; }
static void op_capture(void *ctx, const yg_record_t *r) { (void)ctx; (void)r; }
static void op_hotspot(void *ctx, bool on) { (void)ctx; (void)on; }
static void op_scan(void *ctx, yg_scan_mode_t m, const yg_beacon_t *b, bool share_radio) { (void)ctx; (void)m; (void)b; (void)share_radio; }
static void op_save_beacon(void *ctx, const yg_beacon_t *b) { (void)ctx; (void)b; }
static void op_save_settings(void *ctx) { (void)ctx; }
static void op_env_start(void *ctx) { (void)ctx; }
static void op_env_finish(void *ctx, yg_env_t *out) { (void)ctx; memset(out, 0, sizeof(*out)); }

static void op_log(void *ctx, const yg_record_t *r) {
    (void)ctx;
    switch (r->type) {
    case YG_REC_PRESS:
        printf("PRESS,%u,%s\n", (unsigned)r->uptime_ms, yg_press_reason_name(r->payload[0]));
        break;
    case YG_REC_REARM:
        printf("REARM,%u,%s\n", (unsigned)r->uptime_ms,
               r->payload[0] == YG_REARM_GONE ? "gone" : r->payload[0] == YG_REARM_STILL ? "still" : "moved");
        break;
    case YG_REC_MOTION:
        printf("MOTION,%u,%s\n", (unsigned)r->uptime_ms, r->payload[0] == YG_MOTION_YAW ? "yaw" : "accel");
        break;
    default:
        break;
    }
}

static void op_factory_reset(void *ctx) { (void)ctx; }

static const yg_engine_ops_t OPS = {
    op_press, op_led, op_buzz, op_log, op_capture, op_hotspot, op_scan,
    op_save_beacon, op_save_settings, op_env_start, op_env_finish, op_factory_reset, NULL,
};

static const yg_addr_t BEACON = { { 0xC0, 0, 0, 0, 0, 1 }, YG_ADDR_TYPE_RANDOM };

static void start(uint32_t t) {
    yg_beacon_t beacon = { true, BEACON };
    yg_boot_info_t boot = { 1, "replay", 0, 0, false };
    settings.hotspot_s = 0;
    yg_engine_init(&engine, &OPS, &settings, &beacon, &boot, t);
    started = true;
}

int main(void) {
    static char line[256];
    yg_settings_defaults(&settings);
    while (fgets(line, sizeof(line), stdin)) {
        char kind = line[0];
        if (strncmp(line, "set,", 4) == 0) {
            char *key = line + 4;
            char *value = strchr(key, ',');
            if (value == NULL) continue;
            *value++ = 0;
            value[strcspn(value, "\r\n")] = 0;
            if (!yg_settings_set_str(&settings, key, value)) {
                fprintf(stderr, "bad setting: %s=%s\n", key, value);
                return 2;
            }
            continue;
        }
        unsigned t;
        if (sscanf(line + 1, ",%u", &t) != 1) continue;
        if (!started) start(t);
        if (kind == 'A') {
            int rssi;
            if (sscanf(line + 1, ",%u,%d", &t, &rssi) == 2) {
                yg_ibeacon_t none = { 0 };
                yg_engine_on_advert(&engine, t, &BEACON, (int8_t)rssi, &none);
            }
        } else if (kind == 'M') {
            bool debug = settings.debug;
            settings.debug = true;
            yg_engine_simulate_motion(&engine, t);
            settings.debug = debug;
        } else if (kind == 'I') {
            yg_imu_sample_t s;
            if (sscanf(line + 1, ",%u,%f,%f,%f,%f,%f,%f", &t, &s.ax, &s.ay, &s.az, &s.gx, &s.gy, &s.gz) == 7) {
                yg_engine_on_imu(&engine, t, &s, 25.0f);
            }
        } else if (kind == 'F' || kind == 'R') {
            yg_trigger_set_sensor_ok(&engine.trigger, kind == 'R');
        } else if (kind == 'T') {
            yg_engine_tick(&engine, t);
        }
    }
    return 0;
}
