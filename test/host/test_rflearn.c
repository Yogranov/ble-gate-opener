#include <stdlib.h>
#include <string.h>

#include "yg_engine.h"
#include "yg_test.h"

/* A fake hardware side for the RF learn flow. */
static struct {
    int rf_saves;
    yg_rfcode_t last_saved;
    bool listening;
    int beeps[YG_BUZZ_COUNT];
} fake;

static void op_press(void *c, uint32_t ms) { (void)c; (void)ms; }
static void op_led(void *c, yg_led_event_t e) { (void)c; (void)e; }
static void op_buzz(void *c, yg_buzz_event_t e) { (void)c; fake.beeps[e]++; }
static void op_log(void *c, const yg_record_t *r) { (void)c; (void)r; }
static void op_capture(void *c, const yg_record_t *r) { (void)c; (void)r; }
static void op_hotspot(void *c, bool on) { (void)c; (void)on; }
static void op_scan(void *c, yg_scan_mode_t m, const yg_beacon_t *b, bool share) { (void)c; (void)m; (void)b; (void)share; }
static void op_save_beacon(void *c, const yg_beacon_t *b) { (void)c; (void)b; }
static void op_save_settings(void *c) { (void)c; }
static void op_env_start(void *c) { (void)c; }
static void op_env_finish(void *c, yg_env_t *out) { (void)c; memset(out, 0, sizeof(*out)); }
static void op_factory_reset(void *c) { (void)c; }
static void op_save_gyro_zero(void *c, const float z[3]) { (void)c; (void)z; }
static void op_save_rf_code(void *c, const yg_rfcode_t *code) { (void)c; fake.rf_saves++; fake.last_saved = *code; }
static void op_rf_listen(void *c, bool on) { (void)c; fake.listening = on; }

static const yg_engine_ops_t OPS = {
    .press = op_press, .led = op_led, .buzz = op_buzz, .log = op_log, .capture = op_capture,
    .hotspot = op_hotspot, .scan = op_scan, .save_beacon = op_save_beacon, .save_settings = op_save_settings,
    .env_start = op_env_start, .env_finish = op_env_finish, .factory_reset = op_factory_reset,
    .save_gyro_zero = op_save_gyro_zero, .save_rf_code = op_save_rf_code, .rf_listen = op_rf_listen, .ctx = NULL,
};

static yg_engine_t *e;
static yg_settings_t s;
static const yg_addr_t GATE = {{0xC5, 0xB0, 0xE6, 0x53, 0x59, 0x02}, YG_ADDR_TYPE_RANDOM};

static uint32_t boot_engine(bool rf_learned) {
    yg_beacon_t b = {true, GATE};
    yg_boot_info_t info = {0};
    info.trip = 1;
    info.fw = "2.0.1";
    info.reset_reason = 1;
    info.sensor_id = 0x68;
    info.rf_learned = rf_learned;
    uint32_t now = 1000;
    if (e == NULL) e = calloc(1, sizeof(*e));
    yg_settings_defaults(&s);
    memset(&fake, 0, sizeof(fake));
    yg_engine_init(e, &OPS, &s, &b, &info, now);
    return now;
}

/* reps copies of frame (odd len) each followed by a long gap. */
static uint16_t build(uint16_t *buf, const uint16_t *frame, uint16_t len, uint16_t reps, uint16_t gap) {
    uint16_t w = 0;
    for (uint16_t r = 0; r < reps; r++) {
        memcpy(&buf[w], frame, len * sizeof(uint16_t));
        w += len;
        buf[w++] = gap;
    }
    return w;
}

static void status_has(const char *needle) {
    char buf[2048];
    yg_engine_status_json(e, 2000, buf, sizeof(buf));
    CHECK_HAS(buf, needle);
}

static void learns_and_saves(void) {
    uint32_t now = boot_engine(false);
    status_has("\"rf_learned\":false");
    yg_engine_rf_learn_start(e, now);
    CHECK(fake.listening);
    CHECK_INT(fake.beeps[YG_BUZZ_PAIRING], 1);
    uint16_t frame[] = {300, 900, 300, 900, 300};
    uint16_t buf[64];
    uint16_t n = build(buf, frame, 5, 5, 10000);
    yg_engine_on_rf_capture(e, now, buf, n, true);
    CHECK_INT(fake.rf_saves, 1);
    CHECK(fake.last_saved.learned);
    CHECK_INT(fake.last_saved.count, 5);
    CHECK(!fake.listening);
    CHECK_INT(fake.beeps[YG_BUZZ_SAVED], 1);
    status_has("\"rf_learned\":true");
}

static void rejects_rolling_and_keeps_listening(void) {
    uint32_t now = boot_engine(false);
    yg_engine_rf_learn_start(e, now);
    uint16_t frames[5][5] = {
        {300, 300, 300, 300, 300},
        {300, 1200, 300, 300, 300},
        {300, 300, 300, 1200, 300},
        {300, 1200, 300, 1200, 300},
        {300, 2400, 300, 300, 300},
    };
    uint16_t buf[64];
    uint16_t w = 0;
    for (uint16_t r = 0; r < 5; r++) {
        memcpy(&buf[w], frames[r], 5 * sizeof(uint16_t));
        w += 5;
        buf[w++] = 10000;
    }
    yg_engine_on_rf_capture(e, now, buf, w, true);
    CHECK_INT(fake.rf_saves, 0);
    CHECK(fake.listening);
    CHECK(fake.beeps[YG_BUZZ_REJECTED] >= 1);
    status_has("\"rf_learned\":false");
}

static void times_out(void) {
    uint32_t now = boot_engine(false);
    yg_engine_rf_learn_start(e, now);
    CHECK(fake.listening);
    uint32_t t = now + (uint32_t)s.pair_timeout_s * 1000u + 100u;
    yg_engine_tick(e, t);
    CHECK(!fake.listening);
}

static void forgets(void) {
    boot_engine(true);
    status_has("\"rf_learned\":true");
    yg_engine_rf_forget(e, 2000);
    CHECK_INT(fake.rf_saves, 1);
    CHECK(!fake.last_saved.learned);
    status_has("\"rf_learned\":false");
}

int main(void) {
    learns_and_saves();
    rejects_rolling_and_keeps_listening();
    times_out();
    forgets();
    printf("%d checks, %d failures\n", yg_test_checks, yg_test_failures);
    return yg_test_failures != 0;
}
