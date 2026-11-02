#include <stdlib.h>

#include "yg_engine.h"
#include "yg_test.h"

#define SEC(x) ((uint32_t)(x) * 1000u)
#define MAX_RECORDS 8000

/* A fake of the hardware side that remembers everything the engine asked for. */
static struct {
    int presses;
    uint32_t last_press_ms;
    int beeps[YG_BUZZ_COUNT];
    yg_led_event_t last_led;
    int led_events;
    yg_record_t *records;
    int record_count;
    int capture_count;
    bool hotspot_on;
    int hotspot_calls;
    yg_scan_mode_t scan;
    bool scan_shares;
    bool shared_before_hotspot_started;
    yg_beacon_t scan_beacon;
    yg_beacon_t saved_beacon;
    int beacon_saves;
    int settings_saves;
    int env_starts;
    int env_finishes;
    int factory_resets;
    bool sounded_before_erasing;
} fake;

static void op_press(void *ctx, uint32_t ms) { (void)ctx; fake.presses++; fake.last_press_ms = ms; }
static void op_led(void *ctx, yg_led_event_t e) { (void)ctx; fake.last_led = e; fake.led_events++; }
static void op_buzz(void *ctx, yg_buzz_event_t e) { (void)ctx; fake.beeps[e]++; }
static void op_log(void *ctx, const yg_record_t *r) {
    (void)ctx;
    if (fake.record_count < MAX_RECORDS) fake.records[fake.record_count++] = *r;
}
static void op_capture(void *ctx, const yg_record_t *r) { (void)ctx; (void)r; fake.capture_count++; }
static void op_hotspot(void *ctx, bool on) { (void)ctx; fake.hotspot_on = on; fake.hotspot_calls++; if (on) fake.shared_before_hotspot_started = fake.scan_shares; }
static void op_scan(void *ctx, yg_scan_mode_t mode, const yg_beacon_t *b, bool share_radio) { (void)ctx; fake.scan = mode; fake.scan_beacon = *b; fake.scan_shares = share_radio; }
static void op_save_beacon(void *ctx, const yg_beacon_t *b) { (void)ctx; fake.saved_beacon = *b; fake.beacon_saves++; }
static void op_save_settings(void *ctx) { (void)ctx; fake.settings_saves++; }
static void op_env_start(void *ctx) { (void)ctx; fake.env_starts++; }
static void op_env_finish(void *ctx, yg_env_t *out) {
    (void)ctx;
    fake.env_finishes++;
    memset(out, 0, sizeof(*out));
    out->ble_devices = 9;
    out->wifi_networks = 4;
}

static void op_factory_reset(void *ctx) {
    (void)ctx;
    fake.factory_resets++;
    fake.sounded_before_erasing = fake.beeps[YG_BUZZ_ERASE] > 0;
}

static const yg_engine_ops_t OPS = {
    op_press, op_led, op_buzz, op_log, op_capture, op_hotspot, op_scan,
    op_save_beacon, op_save_settings, op_env_start, op_env_finish, op_factory_reset, NULL,
};

static yg_engine_t *e;
static yg_settings_t s;
static uint32_t now;
static const yg_addr_t GATE = { { 0xC5, 0xB0, 0xE6, 0x53, 0x59, 0x02 }, YG_ADDR_TYPE_RANDOM };
static const yg_addr_t OTHER = { { 0xD1, 0x22, 0x33, 0x44, 0x55, 0x66 }, YG_ADDR_TYPE_RANDOM };
static yg_ibeacon_t ib = { true, { 0 }, 1, 2, -59 };
static float gyro_zero[3];

static void boot(bool paired) {
    yg_beacon_t b = { paired, GATE };
    yg_boot_info_t info = { 7, "2.0.0", 1, 0x68, false };
    now = 1000;
    fake.presses = 0;
    if (e == NULL) e = calloc(1, sizeof(*e));
    yg_engine_init(e, &OPS, &s, &b, &info, now);
}

static void setup(bool paired) {
    if (fake.records == NULL) fake.records = malloc(sizeof(yg_record_t) * MAX_RECORDS);
    yg_record_t *records = fake.records;
    memset(&fake, 0, sizeof(fake));
    fake.records = records;
    yg_settings_defaults(&s);
    memset(gyro_zero, 0, sizeof(gyro_zero));
    boot(paired);
}

static int imu_counter;

/* Time passes in 20 ms IMU steps with a tick every 100 ms. `accel` is the forward
 * acceleration in g and `yaw` the turn rate. advert_ms 0 means no adverts. */
static void run(uint32_t ms, float accel, float yaw, uint32_t advert_ms, int8_t rssi) {
    for (uint32_t t = 0; t < ms; t += 20) {
        imu_counter++;
        yg_imu_sample_t in = { accel, 0.0005f * (float)(imu_counter % 3), 1.0f, gyro_zero[0] + 0.02f * (float)(imu_counter % 2),
                               gyro_zero[1], gyro_zero[2] + yaw };
        yg_engine_on_imu(e, now, &in, 33.5f);
        if (advert_ms && t % advert_ms == 0) yg_engine_on_advert(e, now, &GATE, rssi, &ib);
        if (t % 100 == 0) yg_engine_tick(e, now);
        now += 20;
    }
}

static void still(uint32_t ms, uint32_t advert_ms, int8_t rssi) { run(ms, 0, 0, advert_ms, rssi); }

/* The sensor stops answering. */
static void sensor_down(uint32_t ms, bool absent, uint32_t advert_ms, int8_t rssi) {
    for (uint32_t t = 0; t < ms; t += 20) {
        yg_engine_on_imu_failed(e, now, absent);
        if (advert_ms && t % advert_ms == 0) yg_engine_on_advert(e, now, &GATE, rssi, &ib);
        if (t % 100 == 0) yg_engine_tick(e, now);
        now += 20;
    }
}
static void pull_out(uint32_t ms, uint32_t advert_ms, int8_t rssi) { run(ms, 0.1f, 0, advert_ms, rssi); }

/* A sensor reset by a power glitch sleeps, and reads as all zeros. */
static void sensor_asleep(uint32_t ms, uint32_t advert_ms, int8_t rssi) {
    const yg_imu_sample_t zeros = { 0, 0, 0, 0, 0, 0 };
    for (uint32_t t = 0; t < ms; t += 20) {
        yg_engine_on_imu(e, now, &zeros, 36.5f);
        if (advert_ms && t % advert_ms == 0) yg_engine_on_advert(e, now, &GATE, rssi, &ib);
        if (t % 100 == 0) yg_engine_tick(e, now);
        now += 20;
    }
}

static int total_beeps(void) {
    int n = 0;
    for (int i = 0; i < YG_BUZZ_COUNT; i++) n += fake.beeps[i];
    return n;
}

static int status_int(const char *key) {
    static char buf[1024];
    CHECK(yg_engine_status_json(e, now, buf, sizeof(buf)) > 0);
    const char *at = strstr(buf, key);
    CHECK(at != NULL);
    return at == NULL ? -1 : atoi(at + strlen(key));
}

static int count_error(uint8_t code) {
    int n = 0;
    for (int i = 0; i < fake.record_count; i++) {
        if (fake.records[i].type == YG_REC_ERROR && fake.records[i].payload[0] == code) n++;
    }
    return n;
}

static int count_records(uint8_t type) {
    int n = 0;
    for (int i = 0; i < fake.record_count; i++) {
        if (fake.records[i].type == type) n++;
    }
    return n;
}

static const yg_record_t *last_record(uint8_t type) {
    for (int i = fake.record_count; i-- > 0;) {
        if (fake.records[i].type == type) return &fake.records[i];
    }
    return NULL;
}

static void boot_is_logged_and_scanning_starts(void) {
    setup(true);
    CHECK_INT(count_records(YG_REC_BOOT), 1);
    CHECK_INT(fake.scan, YG_SCAN_FILTERED);
    CHECK(fake.scan_beacon.paired);
    CHECK_INT(fake.presses, 0);
}

static void owners_failure_ignition_in_spot_never_presses_and_pull_out_presses_once(void) {
    setup(true);
    still(SEC(60), 300, -91);
    CHECK_INT(fake.presses, 0);
    pull_out(SEC(2), 300, -90);
    CHECK_INT(fake.presses, 1);
    CHECK_INT(fake.last_press_ms, 2000);
    pull_out(SEC(3), 300, -80);
    still(SEC(20), 300, -60);
    CHECK_INT(fake.presses, 1);
    const yg_record_t *r = last_record(YG_REC_PRESS);
    CHECK(r != NULL);
    if (r != NULL) CHECK_INT(r->payload[0], YG_PRESS_AUTO);
}

static void weak_day_presses_when_the_beacon_is_first_heard_on_the_way(void) {
    setup(true);
    still(SEC(30), 0, 0);
    pull_out(SEC(5), 0, 0);
    CHECK_INT(fake.presses, 0);
    pull_out(SEC(1), 300, -92);
    CHECK_INT(fake.presses, 1);
}

static void arriving_from_the_street_presses_on_first_advert(void) {
    setup(true);
    still(SEC(5), 0, 0);
    for (int i = 0; i < 10; i++) {
        pull_out(SEC(4), 0, 0);
        still(SEC(10), 0, 0);
    }
    CHECK_INT(fake.presses, 0);
    pull_out(SEC(2), 300, -91);
    CHECK_INT(fake.presses, 1);
}

static void stopped_at_a_closed_gate_still_presses(void) {
    setup(true);
    still(SEC(3), 0, 0);
    pull_out(SEC(3), 0, 0);
    still(SEC(30), 0, 0);
    CHECK_INT(fake.presses, 0);
    still(SEC(1), 300, -85);
    CHECK_INT(fake.presses, 1);
}

static void after_a_press_quiet_until_still_for_two_minutes(void) {
    setup(true);
    still(SEC(3), 300, -85);
    pull_out(SEC(2), 300, -85);
    CHECK_INT(fake.presses, 1);
    still(SEC(100), 300, -85);
    CHECK_INT(count_records(YG_REC_REARM), 0);
    still(SEC(30), 300, -85);
    CHECK_INT(count_records(YG_REC_REARM), 1);
    CHECK_INT(fake.presses, 1);
    pull_out(SEC(2), 300, -85);
    CHECK_INT(fake.presses, 2);
}

static void after_a_press_rearms_once_the_beacon_is_gone(void) {
    setup(true);
    still(SEC(3), 300, -85);
    pull_out(SEC(2), 300, -85);
    CHECK_INT(fake.presses, 1);
    for (int i = 0; i < 6; i++) {
        pull_out(SEC(3), 0, 0);
        still(SEC(8), 0, 0);
    }
    CHECK_INT(count_records(YG_REC_REARM), 1);
    pull_out(SEC(2), 300, -85);
    CHECK_INT(fake.presses, 2);
}

static void next_start_begins_fresh(void) {
    setup(true);
    still(SEC(3), 300, -85);
    pull_out(SEC(2), 300, -85);
    CHECK_INT(fake.presses, 1);
    boot(true);
    still(SEC(30), 300, -85);
    CHECK_INT(fake.presses, 0);
}

static void set_point_stops_a_press_from_driving_past(void) {
    setup(true);
    s.set_point_dbm = -80;
    still(SEC(3), 0, 0);
    pull_out(SEC(20), 300, -90);
    CHECK_INT(fake.presses, 0);
    pull_out(SEC(1), 300, -79);
    CHECK_INT(fake.presses, 1);
}

static void other_devices_never_count_as_the_beacon(void) {
    setup(true);
    still(SEC(3), 0, 0);
    pull_out(SEC(2), 0, 0);
    yg_engine_on_advert(e, now, &OTHER, -40, &ib);
    CHECK_INT(fake.presses, 0);
    CHECK_INT(count_records(YG_REC_FIRST_HEARD), 0);
}

static void sensor_failure_falls_back_to_the_threshold(void) {
    setup(true);
    still(SEC(3), 0, 0);
    CHECK_INT(fake.last_led, YG_LED_OFF);
    sensor_down(SEC(6), false, 0, 0);
    CHECK_INT(count_error(YG_ERROR_SENSOR_READS), 1);
    CHECK_INT(fake.last_led, YG_LED_SENSOR_FAILED);
    yg_engine_on_advert(e, now, &GATE, -95, &ib);
    CHECK_INT(fake.presses, 0);
    yg_engine_on_advert(e, now, &GATE, -85, &ib);
    CHECK_INT(fake.presses, 1);
    const yg_record_t *r = last_record(YG_REC_PRESS);
    CHECK(r != NULL);
    if (r != NULL) CHECK_INT(r->payload[0], YG_PRESS_FALLBACK);
}

static void absent_sensor_falls_back_at_boot(void) {
    /* Not at the first advert: only once it has really been missing for a while. */
    setup(true);
    sensor_down(4800, true, 300, -85);
    CHECK_INT(fake.presses, 0);
    CHECK_INT(count_records(YG_REC_ERROR), 0);
    sensor_down(400, true, 0, 0);
    CHECK_INT(count_error(YG_ERROR_SENSOR_ABSENT), 1);
    CHECK_INT(fake.last_led, YG_LED_SENSOR_FAILED);
    yg_engine_on_advert(e, now, &GATE, -85, &ib);
    CHECK_INT(fake.presses, 1);
}

static void short_sensor_dropouts_never_press_a_parked_car(void) {
    setup(true);
    still(SEC(3), 300, -85);
    for (int i = 0; i < 5; i++) {
        sensor_down(200, false, 300, -85);
        still(SEC(130), 300, -85);
    }
    CHECK_INT(fake.presses, 0);
    CHECK_INT(count_error(YG_ERROR_SENSOR_READS), 0);
}

static void short_outages_are_logged_with_their_length(void) {
    setup(true);
    still(SEC(3), 0, 0);
    sensor_down(400, false, 0, 0);
    still(SEC(1), 0, 0);
    CHECK_INT(count_error(YG_ERROR_SENSOR_GLITCH), 1);
    const yg_record_t *r = last_record(YG_REC_ERROR);
    CHECK(r != NULL);
    if (r != NULL) CHECK_INT(yg_get_u32(r->payload + 1), 400);
    /* Not one record per glitch from a loose wire. */
    for (int i = 0; i < 3; i++) {
        sensor_down(200, false, 0, 0);
        still(SEC(1), 0, 0);
    }
    CHECK_INT(count_error(YG_ERROR_SENSOR_GLITCH), 1);
    still(SEC(10), 0, 0);
    sensor_down(200, false, 0, 0);
    still(SEC(1), 0, 0);
    CHECK_INT(count_error(YG_ERROR_SENSOR_GLITCH), 2);
    /* A single lost read is nothing to report. */
    still(SEC(11), 0, 0);
    sensor_down(20, false, 0, 0);
    still(SEC(1), 0, 0);
    CHECK_INT(count_error(YG_ERROR_SENSOR_GLITCH), 2);
}

static void a_sleeping_sensor_presses_nothing(void) {
    setup(true);
    gyro_zero[0] = 3.0f;
    gyro_zero[1] = -2.0f;
    gyro_zero[2] = 1.0f;
    still(SEC(10), 300, -85);
    sensor_asleep(SEC(1), 300, -85);
    still(SEC(30), 300, -85);
    CHECK_INT(fake.presses, 0);
    CHECK_INT(count_records(YG_REC_MOTION), 0);
}

static void a_sensor_asleep_at_power_on_does_not_fall_back(void) {
    /* One write failed while configuring it, so it sleeps until it is woken. */
    setup(true);
    sensor_asleep(SEC(1), 300, -85);
    still(SEC(30), 300, -85);
    CHECK_INT(fake.presses, 0);
    CHECK_INT(count_error(YG_ERROR_SENSOR_GRAVITY), 0);
}

static void a_sensor_that_stays_asleep_counts_as_failed(void) {
    setup(true);
    still(SEC(3), 0, 0);
    sensor_asleep(SEC(6), 0, 0);
    CHECK_INT(count_error(YG_ERROR_SENSOR_READS), 1);
    CHECK_INT(fake.last_led, YG_LED_SENSOR_FAILED);
}

static void the_driver_leaving_after_a_fallback_press_still_presses(void) {
    setup(true);
    still(SEC(60), 300, -85);
    sensor_down(SEC(6), false, 300, -85);
    CHECK_INT(fake.presses, 1);
    still(SEC(300), 300, -85);
    CHECK_INT(fake.presses, 1);
    pull_out(SEC(2), 300, -85);
    CHECK_INT(fake.presses, 2);
    const yg_record_t *r = last_record(YG_REC_REARM);
    CHECK(r != NULL);
    if (r != NULL) CHECK_INT(r->payload[0], YG_REARM_MOVED);
}

static void a_fallback_press_stays_single_after_the_sensor_recovers(void) {
    setup(true);
    still(SEC(3), 300, -85);
    sensor_down(SEC(6), false, 300, -85);
    CHECK_INT(fake.presses, 1);
    still(SEC(300), 300, -85);
    CHECK_INT(fake.presses, 1);
    CHECK_INT(count_records(YG_REC_REARM), 0);
    still(SEC(70), 0, 0);
    CHECK_INT(count_records(YG_REC_REARM), 1);
    still(SEC(5), 300, -85);
    CHECK_INT(fake.presses, 1);
}

static void the_motion_estimate_starts_over_after_an_outage(void) {
    /* The unit may have been moved while the sensor was gone. */
    setup(true);
    still(SEC(3), 0, 0);
    sensor_down(SEC(6), false, 0, 0);
    run(SEC(20), 0.1f, 0, 0, 0);
    CHECK_INT(count_records(YG_REC_MOTION), 0);
    run(SEC(5), 0.1f, 0, 300, -85);
    CHECK_INT(fake.presses, 0);
}

static void set_point_here_forgets_the_old_beacon(void) {
    setup(true);
    for (int i = 0; i < 5; i++) yg_engine_on_advert(e, now, &GATE, -40, &ib);
    yg_engine_pair_set(e, now, OTHER.addr, OTHER.type);
    CHECK(!yg_engine_setpoint_here(e, now));
    yg_engine_on_advert(e, now, &OTHER, -80, &ib);
    CHECK(yg_engine_setpoint_here(e, now));
    CHECK_INT(s.set_point_dbm, -80);
}

static void sensor_recovery_is_logged_and_restores_the_rule(void) {
    setup(true);
    still(SEC(3), 0, 0);
    sensor_down(SEC(6), false, 0, 0);
    still(SEC(6), 0, 0);
    CHECK_INT(count_error(YG_ERROR_SENSOR_READS), 1);
    CHECK_INT(count_error(YG_ERROR_SENSOR_RECOVERED), 1);
    CHECK_INT(fake.last_led, YG_LED_OFF);
    yg_engine_on_advert(e, now, &GATE, -85, &ib);
    CHECK_INT(fake.presses, 0);
}

static void unpaired_never_auto_presses_and_does_not_scan(void) {
    setup(false);
    CHECK_INT(fake.scan, YG_SCAN_OFF);
    CHECK_INT(fake.last_led, YG_LED_UNPAIRED);
    still(SEC(2), 0, 0);
    pull_out(SEC(3), 300, -40);
    CHECK_INT(fake.presses, 0);
    CHECK(yg_engine_press(e, now, YG_PRESS_WEB));
    CHECK_INT(fake.presses, 1);
}

static void pairing_by_holding_close_saves_and_returns_to_filtered_scan(void) {
    setup(false);
    s.set_point_dbm = -70;
    still(SEC(3), 0, 0);
    yg_engine_on_gesture(e, now, YG_GESTURE_HOLD);
    CHECK_INT(fake.scan, YG_SCAN_OPEN);
    CHECK_INT(fake.last_led, YG_LED_PAIRING);
    for (int i = 0; i < 20; i++) {
        yg_engine_on_advert(e, now, &GATE, -40, &ib);
        run(200, 0, 0, 0, 0);
    }
    CHECK_INT(fake.beacon_saves, 1);
    CHECK(fake.saved_beacon.paired);
    CHECK(yg_addr_equal(&fake.saved_beacon.addr, &GATE));
    CHECK_INT(fake.scan, YG_SCAN_FILTERED);
    CHECK_INT(s.set_point_dbm, YG_SET_POINT_ANY);
    CHECK(fake.settings_saves >= 1);
    CHECK_INT(fake.last_led, YG_LED_SAVED);
    CHECK_INT(count_records(YG_REC_PAIR), 1);
}

static void pairing_timeout_keeps_the_old_beacon(void) {
    setup(true);
    yg_engine_pair_start(e, now, YG_PAIR_AUTO);
    still(SEC(61), 0, 0);
    CHECK_INT(fake.beacon_saves, 0);
    CHECK_INT(fake.scan, YG_SCAN_FILTERED);
    CHECK(fake.scan_beacon.paired);
    CHECK_INT(fake.last_led, YG_LED_REJECTED);
    CHECK_INT(count_records(YG_REC_PAIR), 1);
}

static void pairing_suspends_presses_but_manual_still_works(void) {
    setup(true);
    still(SEC(3), 0, 0);
    yg_engine_pair_start(e, now, YG_PAIR_LIST);
    pull_out(SEC(2), 0, 0);
    yg_engine_on_advert(e, now, &GATE, -60, &ib);
    CHECK_INT(fake.presses, 0);
    CHECK(yg_engine_press(e, now, YG_PRESS_WEB));
    CHECK_INT(fake.presses, 1);
    yg_engine_pair_cancel(e, now);
    CHECK_INT(fake.scan, YG_SCAN_FILTERED);
}

static void typed_mac_saves_with_any_type(void) {
    setup(false);
    yg_engine_pair_set(e, now, GATE.addr, YG_ADDR_TYPE_ANY);
    CHECK(fake.saved_beacon.paired);
    CHECK_INT(fake.saved_beacon.addr.type, YG_ADDR_TYPE_ANY);
    CHECK_INT(fake.scan, YG_SCAN_FILTERED);
    CHECK_INT(fake.scan_beacon.addr.type, YG_ADDR_TYPE_ANY);
    still(SEC(3), 0, 0);
    pull_out(SEC(2), 300, -85);
    CHECK_INT(fake.presses, 1);
}

static void forgetting_stops_presses_and_scanning(void) {
    setup(true);
    yg_engine_pair_forget(e, now);
    CHECK(!fake.saved_beacon.paired);
    CHECK_INT(fake.beacon_saves, 1);
    CHECK_INT(fake.scan, YG_SCAN_OFF);
    still(SEC(2), 0, 0);
    pull_out(SEC(3), 300, -40);
    CHECK_INT(fake.presses, 0);
}

static void set_point_here_uses_the_strongest_recent_advert(void) {
    setup(true);
    still(SEC(5), 0, 0);
    CHECK(!yg_engine_setpoint_here(e, now));
    CHECK_INT(fake.last_led, YG_LED_REJECTED);
    CHECK_INT(s.set_point_dbm, YG_SET_POINT_ANY);

    yg_engine_on_advert(e, now, &GATE, -60, &ib);
    still(SEC(4), 0, 0);
    yg_engine_on_advert(e, now, &GATE, -82, &ib);
    still(SEC(1), 0, 0);
    yg_engine_on_advert(e, now, &GATE, -78, &ib);
    still(SEC(1), 0, 0);
    yg_engine_on_advert(e, now, &GATE, -80, &ib);
    CHECK(yg_engine_setpoint_here(e, now));
    CHECK_INT(s.set_point_dbm, -78);
    CHECK_INT(fake.last_led, YG_LED_SAVED);
    CHECK(fake.settings_saves >= 1);
    CHECK_INT(count_records(YG_REC_SETTING), 1);
}

/* The bench unit's own name, which only the hardware layer can work out. */
#define UNIT_SSID "YogiGate-0F01"

static void reset_to_defaults_restores_every_setting(void) {
    setup(true);
    s.press_ms = 500;
    s.set_point_dbm = -70;
    s.buzzer_enabled = false;
    s.buzzer_type = YG_BUZZER_PASSIVE;
    s.hotspot_s = 300;
    s.motion_accel_mg = 100;
    strcpy(s.hotspot_ssid, "Custom");
    strcpy(s.hotspot_password, "secret123");

    yg_engine_settings_reset(e, now, UNIT_SSID);

    yg_settings_t wanted;
    yg_settings_defaults(&wanted);
    strcpy(wanted.hotspot_ssid, UNIT_SSID);
    CHECK(memcmp(&s, &wanted, sizeof(s)) == 0);
    CHECK(strcmp(s.hotspot_password, "password") == 0);
    CHECK_INT(fake.settings_saves, 1);
}

static void reset_to_defaults_keeps_the_beacon_and_presses_nothing(void) {
    setup(true);
    s.buzzer_enabled = false;
    still(SEC(3), 300, -85);
    uint32_t trip = e->trip;

    yg_engine_settings_reset(e, now, UNIT_SSID);

    CHECK(e->beacon.paired);
    CHECK_INT(fake.beacon_saves, 0);
    CHECK_INT(e->trip, trip);
    CHECK_INT(fake.presses, 0);
    CHECK_INT(e->trigger.state, YG_TRIG_ARMED);
    CHECK_INT(fake.scan, YG_SCAN_FILTERED);
}

static void reset_to_defaults_logs_only_what_changed(void) {
    setup(true);
    strcpy(s.hotspot_ssid, UNIT_SSID);
    int before = fake.record_count;
    yg_engine_settings_reset(e, now, UNIT_SSID);
    CHECK_INT(fake.record_count, before);

    s.press_ms = 500;
    strcpy(s.hotspot_password, "secret123");
    yg_engine_settings_reset(e, now, UNIT_SSID);
    CHECK_INT(fake.record_count, before + 2);
    /* The old value of a password never reaches a log, and neither does the new one. */
    const yg_record_t *last = &fake.records[fake.record_count - 1];
    CHECK(memcmp(last->payload, "hotspot_password", 17) == 0);
    CHECK(strcmp((const char *)last->payload + 17, "changed") == 0);
    const yg_record_t *first = &fake.records[fake.record_count - 2];
    CHECK(memcmp(first->payload, "press_ms", 9) == 0);
    CHECK(strcmp((const char *)first->payload + 9, "2000") == 0);
}

static void reset_to_defaults_answers_with_success(void) {
    setup(true);
    s.buzzer_enabled = false;
    s.led_enabled = false;
    yg_engine_settings_reset(e, now, UNIT_SSID);
    CHECK_INT(fake.beeps[YG_BUZZ_SAVED], 1);
    CHECK_INT(fake.last_led, YG_LED_SAVED);
}

static void reset_to_defaults_ends_debug_captures(void) {
    setup(true);
    s.debug = true;
    yg_engine_setting_changed(e, now, "debug", "1");
    CHECK(e->capture != NULL);
    yg_engine_settings_reset(e, now, UNIT_SSID);
    CHECK(!s.debug);
    CHECK(e->capture == NULL);
}

static void restarting_the_trip_count_makes_this_trip_number_one(void) {
    setup(true);
    still(SEC(3), 300, -85);
    CHECK_INT(e->trip, 7);
    CHECK_INT(count_records(YG_REC_FIRST_HEARD), 1);
    fake.record_count = 0;

    yg_engine_trips_restart(e);
    CHECK_INT(e->trip, 1);
    CHECK_INT(status_int("\"trip\":"), 1);
    /* The erase takes a while, and whatever is logged during it must not carry the old number,
     * including a trace that was already being collected. */
    still(SEC(8), 300, -85);
    CHECK(fake.record_count > 0);
    for (int i = 0; i < fake.record_count; i++) {
        CHECK_INT(fake.records[i].trip, 1);
    }

    /* The erased log took this trip's boot record with it, and a replay starts from that. */
    fake.record_count = 0;
    yg_engine_log_restarted(e, now);
    CHECK_INT(count_records(YG_REC_BOOT), 1);
    still(SEC(2), 300, -85);
    CHECK_INT(count_records(YG_REC_FIRST_HEARD), 1);
    for (int i = 0; i < fake.record_count; i++) {
        CHECK_INT(fake.records[i].trip, 1);
    }
}

static void restarting_the_trip_count_changes_nothing_else(void) {
    setup(true);
    still(SEC(3), 300, -85);
    yg_engine_trips_restart(e);
    yg_engine_log_restarted(e, now);
    CHECK_INT(fake.presses, 0);
    CHECK_INT(e->trigger.state, YG_TRIG_ARMED);
    CHECK(e->beacon.paired);
    CHECK_INT(fake.beacon_saves, 0);
    CHECK_INT(fake.settings_saves, 0);
    /* The rule still works afterwards. */
    pull_out(SEC(2), 300, -85);
    CHECK_INT(fake.presses, 1);
}

static void scanning_has_the_whole_radio_with_the_hotspot_off(void) {
    setup(true);
    still(SEC(3), 300, -85);
    CHECK_INT(fake.scan, YG_SCAN_FILTERED);
    CHECK(!fake.scan_shares);
}

/* Measured on a real unit: a scan at full time left a phone on the page no requests at all. */
static void scanning_shares_the_radio_while_the_hotspot_is_on(void) {
    setup(true);
    yg_engine_on_gesture(e, now, YG_GESTURE_TRIPLE);
    still(SEC(1), 0, 0);
    CHECK(fake.hotspot_on);
    CHECK(fake.scan_shares);
    CHECK_INT(fake.scan, YG_SCAN_FILTERED);
    CHECK(fake.shared_before_hotspot_started);

    still(SEC(300), 0, 0);
    CHECK(!fake.hotspot_on);
    CHECK(!fake.scan_shares);
    CHECK_INT(fake.scan, YG_SCAN_FILTERED);
}

static void a_scan_for_pairing_shares_the_radio_too(void) {
    setup(true);
    yg_engine_on_gesture(e, now, YG_GESTURE_TRIPLE);
    still(SEC(1), 0, 0);
    yg_engine_pair_start(e, now, YG_PAIR_LIST);
    CHECK_INT(fake.scan, YG_SCAN_OPEN);
    CHECK(fake.scan_shares);
    yg_engine_pair_cancel(e, now);
    CHECK_INT(fake.scan, YG_SCAN_FILTERED);
    CHECK(fake.scan_shares);
}

static void the_hotspot_does_not_start_a_scan_with_no_beacon(void) {
    setup(false);
    yg_engine_on_gesture(e, now, YG_GESTURE_TRIPLE);
    still(SEC(1), 0, 0);
    CHECK(fake.hotspot_on);
    CHECK_INT(fake.scan, YG_SCAN_OFF);
}

static void the_hotspot_keeps_a_debug_snapshot_scanning(void) {
    setup(true);
    s.debug = true;
    yg_engine_setting_changed(e, now, "debug", "1");
    still(SEC(11), 0, 0);
    CHECK_INT(fake.scan, YG_SCAN_OPEN);
    CHECK(!fake.scan_shares);
    yg_engine_on_gesture(e, now, YG_GESTURE_TRIPLE);
    still(SEC(1), 0, 0);
    CHECK_INT(fake.scan, YG_SCAN_OPEN);
    CHECK(fake.scan_shares);
    still(SEC(10), 0, 0);
    CHECK_INT(fake.scan, YG_SCAN_FILTERED);
    CHECK(fake.scan_shares);
}

/* Set here is always used with the hotspot on, when far fewer adverts are heard. */
static void set_here_looks_further_back_while_sharing_the_radio(void) {
    setup(true);
    yg_engine_on_advert(e, now, &GATE, -77, &ib);
    still(SEC(4), 0, 0);
    CHECK(!yg_engine_setpoint_here(e, now));

    setup(true);
    yg_engine_on_gesture(e, now, YG_GESTURE_TRIPLE);
    still(SEC(1), 0, 0);
    yg_engine_on_advert(e, now, &GATE, -77, &ib);
    still(SEC(8), 0, 0);
    CHECK(yg_engine_setpoint_here(e, now));
    CHECK_INT(s.set_point_dbm, -77);
}

static void the_gate_still_opens_while_sharing_the_radio(void) {
    setup(true);
    yg_engine_on_gesture(e, now, YG_GESTURE_TRIPLE);
    still(SEC(3), 1200, -85);
    CHECK_INT(fake.presses, 0);
    pull_out(SEC(2), 1200, -85);
    CHECK_INT(fake.presses, 1);
}

static void set_point_by_value_and_any(void) {
    setup(true);
    CHECK(yg_engine_setpoint_dbm(e, now, -85));
    CHECK_INT(s.set_point_dbm, -85);
    CHECK(yg_engine_setpoint_dbm(e, now, YG_SET_POINT_ANY));
    CHECK_INT(s.set_point_dbm, YG_SET_POINT_ANY);
    CHECK(!yg_engine_setpoint_dbm(e, now, -20));
    CHECK(!yg_engine_setpoint_dbm(e, now, 200));
    CHECK(!yg_engine_setpoint_dbm(e, now, 256));
    CHECK_INT(s.set_point_dbm, YG_SET_POINT_ANY);
}

static void automatic_presses_are_silent(void) {
    setup(true);
    still(SEC(3), 300, -85);
    pull_out(SEC(2), 300, -85);
    CHECK_INT(fake.presses, 1);
    CHECK_INT(total_beeps(), 0);
}

static void fallback_presses_are_silent(void) {
    setup(true);
    s.motion_enabled = false;
    still(SEC(1), 300, -85);
    CHECK_INT(fake.presses, 1);
    CHECK_INT(total_beeps(), 0);
}

static void presses_by_hand_beep(void) {
    setup(true);
    yg_engine_on_gesture(e, now, YG_GESTURE_SINGLE);
    CHECK_INT(fake.beeps[YG_BUZZ_PRESS], 1);
    still(SEC(3), 0, 0);
    CHECK(yg_engine_press(e, now, YG_PRESS_WEB));
    CHECK_INT(fake.beeps[YG_BUZZ_PRESS], 2);
}

static void asking_for_the_hotspot_answers_with_sound_and_light(void) {
    setup(true);
    yg_engine_on_gesture(e, now, YG_GESTURE_TRIPLE);
    CHECK_INT(fake.beeps[YG_BUZZ_HOTSPOT], 1);
    CHECK_INT(fake.last_led, YG_LED_HOTSPOT);
}

static void holding_the_button_answers_that_pairing_started(void) {
    setup(true);
    yg_engine_on_gesture(e, now, YG_GESTURE_HOLD);
    CHECK_INT(fake.beeps[YG_BUZZ_PAIRING], 1);

    setup(true);
    yg_engine_pair_start(e, now, YG_PAIR_LIST);
    CHECK_INT(fake.beeps[YG_BUZZ_PAIRING], 0);
}

static void holding_the_button_15_s_sounds_and_then_erases(void) {
    setup(true);
    yg_engine_on_gesture(e, now, YG_GESTURE_FACTORY_RESET);
    CHECK_INT(fake.beeps[YG_BUZZ_ERASE], 1);
    CHECK_INT(fake.last_led, YG_LED_ERASING);
    CHECK_INT(fake.factory_resets, 1);
    CHECK(fake.sounded_before_erasing);
    CHECK_INT(fake.presses, 0);
}

static void a_factory_reset_does_not_depend_on_the_buzzer_or_led(void) {
    setup(true);
    s.buzzer_enabled = false;
    s.led_enabled = false;
    yg_engine_on_gesture(e, now, YG_GESTURE_FACTORY_RESET);
    CHECK_INT(fake.beeps[YG_BUZZ_ERASE], 0);
    CHECK_INT(fake.led_events, 0);
    CHECK_INT(fake.factory_resets, 1);
}

static void pairing_results_have_their_own_sounds(void) {
    setup(true);
    yg_engine_pair_set(e, now, GATE.addr, GATE.type);
    CHECK_INT(fake.beeps[YG_BUZZ_SAVED], 1);

    setup(true);
    yg_engine_pair_start(e, now, YG_PAIR_AUTO);
    still(SEC(61), 0, 0);
    CHECK_INT(fake.beeps[YG_BUZZ_REJECTED], 1);
}

static void starting_with_no_beacon_sounds_only_then(void) {
    setup(false);
    CHECK_INT(fake.beeps[YG_BUZZ_NO_BEACON], 1);
    still(SEC(5), 0, 0);
    CHECK_INT(fake.beeps[YG_BUZZ_NO_BEACON], 1);

    setup(true);
    still(SEC(5), 0, 0);
    CHECK_INT(total_beeps(), 0);
}

static void a_failed_sensor_sounds_once_per_trip(void) {
    setup(true);
    sensor_down(SEC(6), true, 0, 0);
    CHECK_INT(fake.beeps[YG_BUZZ_SENSOR], 1);
    still(SEC(7), 0, 0);
    sensor_down(SEC(6), true, 0, 0);
    CHECK_INT(fake.beeps[YG_BUZZ_SENSOR], 1);
}

static void a_sensor_turned_off_on_purpose_stays_quiet(void) {
    setup(true);
    s.motion_enabled = false;
    sensor_down(SEC(6), true, 0, 0);
    CHECK_INT(fake.beeps[YG_BUZZ_SENSOR], 0);
}

static void turning_the_buzzer_off_silences_everything(void) {
    setup(true);
    s.buzzer_enabled = false;
    boot(false);
    yg_engine_on_gesture(e, now, YG_GESTURE_SINGLE);
    yg_engine_on_gesture(e, now, YG_GESTURE_DOUBLE);
    yg_engine_on_gesture(e, now, YG_GESTURE_TRIPLE);
    yg_engine_on_gesture(e, now, YG_GESTURE_HOLD);
    still(SEC(61), 0, 0);
    sensor_down(SEC(6), true, 0, 0);
    CHECK_INT(total_beeps(), 0);
}

static void status_holds_motion_peaks_for_the_live_graph(void) {
    setup(true);
    still(SEC(3), 0, 0);
    CHECK_INT(status_int("\"motion_accel_mg\":"), 40);
    CHECK_INT(status_int("\"motion_yaw_deg\":"), 15);
    CHECK(status_int("\"motion_mg_peak\":") < 20);

    pull_out(SEC(1), 0, 0);
    int peak = status_int("\"motion_mg_peak\":");
    CHECK(peak >= 40);
    /* The page reads this twice a second, so a short peak has to stay long enough to be seen. */
    still(400, 0, 0);
    CHECK_INT(status_int("\"motion_mg_peak\":"), peak);
    still(SEC(5), 0, 0);
    CHECK(status_int("\"motion_mg_peak\":") < 20);

    run(SEC(1), 0, 30.0f, 0, 0);
    CHECK(status_int("\"motion_yaw_peak\":") >= 15);
    run(SEC(1), 0, -30.0f, 0, 0);
    CHECK(status_int("\"motion_yaw_peak\":") >= 0);
}

static void led_off_suppresses_led_events(void) {
    setup(false);
    int before = fake.led_events;
    s.led_enabled = false;
    yg_engine_pair_start(e, now, YG_PAIR_AUTO);
    CHECK_INT(fake.led_events, before);
    yg_engine_pair_cancel(e, now);
    CHECK(yg_engine_press(e, now, YG_PRESS_WEB));
    CHECK_INT(fake.led_events, before);
}

static void press_lights_the_led_and_it_goes_out_after(void) {
    setup(true);
    CHECK(yg_engine_press(e, now, YG_PRESS_WEB));
    CHECK_INT(fake.last_led, YG_LED_PRESS);
    still(SEC(3), 0, 0);
    CHECK_INT(fake.last_led, YG_LED_OFF);
}

static void every_gesture_does_its_job(void) {
    setup(true);
    yg_engine_on_gesture(e, now, YG_GESTURE_SINGLE);
    CHECK_INT(fake.presses, 1);
    const yg_record_t *r = last_record(YG_REC_PRESS);
    CHECK(r != NULL);
    if (r != NULL) CHECK_INT(r->payload[0], YG_PRESS_BUTTON);

    yg_engine_on_advert(e, now, &GATE, -77, &ib);
    yg_engine_on_gesture(e, now, YG_GESTURE_DOUBLE);
    CHECK_INT(s.set_point_dbm, -77);

    s.hotspot_s = 0;
    yg_hotspot_init(&e->hotspot, &s, now, false);
    still(SEC(1), 0, 0);
    CHECK(!fake.hotspot_on);
    yg_engine_on_gesture(e, now, YG_GESTURE_TRIPLE);
    still(SEC(1), 0, 0);
    CHECK(fake.hotspot_on);

    yg_engine_on_gesture(e, now, YG_GESTURE_HOLD);
    CHECK_INT(fake.scan, YG_SCAN_OPEN);
}

static void hotspot_stays_off_until_someone_asks(void) {
    setup(true);
    still(SEC(5), 0, 0);
    CHECK(!fake.hotspot_on);
    CHECK_INT(count_records(YG_REC_HOTSPOT), 0);
    yg_engine_on_gesture(e, now, YG_GESTURE_TRIPLE);
    still(SEC(1), 0, 0);
    CHECK(fake.hotspot_on);
    pull_out(SEC(2), 0, 0);
    CHECK(fake.hotspot_on);
    still(SEC(300), 0, 0);
    CHECK(!fake.hotspot_on);
}

static void a_timed_hotspot_starts_on_and_stops_when_the_car_moves(void) {
    setup(true);
    s.hotspot_s = 300;
    yg_hotspot_init(&e->hotspot, &s, now, false);
    still(SEC(2), 0, 0);
    CHECK(fake.hotspot_on);
    pull_out(SEC(2), 0, 0);
    CHECK(!fake.hotspot_on);
    CHECK_INT(count_records(YG_REC_HOTSPOT), 2);
}

static void hotspot_stays_while_a_client_is_connected(void) {
    setup(true);
    s.hotspot_s = 300;
    yg_hotspot_init(&e->hotspot, &s, now, false);
    still(SEC(2), 0, 0);
    yg_engine_on_clients(e, now, 1);
    pull_out(SEC(2), 0, 0);
    CHECK(fake.hotspot_on);
    yg_engine_on_clients(e, now, 0);
    still(SEC(29), 0, 0);
    CHECK(fake.hotspot_on);
    still(SEC(2), 0, 0);
    CHECK(!fake.hotspot_on);
}

static void first_heard_logs_once_and_motion_logs_are_limited(void) {
    setup(true);
    still(SEC(5), 300, -91);
    CHECK_INT(count_records(YG_REC_FIRST_HEARD), 1);
    pull_out(SEC(9), 300, -90);
    CHECK_INT(count_records(YG_REC_MOTION), 1);
    pull_out(SEC(3), 300, -80);
    CHECK_INT(count_records(YG_REC_MOTION), 2);
}

static void temperature_logs_once_a_minute(void) {
    setup(true);
    still(SEC(125), 0, 0);
    CHECK_INT(count_records(YG_REC_TEMP), 3);
}

static void traces_reach_the_log(void) {
    setup(true);
    still(SEC(30), 300, -91);
    CHECK(count_records(YG_REC_TRACE) >= 4);
}

static void debug_captures_around_motion_and_press(void) {
    setup(true);
    s.debug = true;
    yg_engine_setting_changed(e, now, "debug", "1");
    still(SEC(3), 300, -85);
    pull_out(SEC(2), 300, -85);
    CHECK_INT(fake.presses, 1);
    still(SEC(40), 300, -85);
    /* About 4 s of history plus 30 s after, at 19 samples per record. */
    CHECK(fake.capture_count > 80);
    still(SEC(5), 0, 0);
}

static void no_captures_without_debug(void) {
    setup(true);
    still(SEC(3), 300, -85);
    pull_out(SEC(2), 300, -85);
    still(SEC(40), 300, -85);
    CHECK_INT(fake.capture_count, 0);
    CHECK(!yg_engine_capture_now(e, now));
    CHECK(!yg_engine_simulate_motion(e, now));
}

static void debug_near_misses_capture_at_a_limited_rate(void) {
    setup(true);
    s.debug = true;
    yg_engine_setting_changed(e, now, "debug", "1");
    still(SEC(3), 0, 0);
    run(300, 0.1f, 0, 0, 0);          /* A shove, too short to be motion. */
    CHECK_INT(count_records(YG_REC_MOTION), 0);
    still(SEC(35), 0, 0);
    int first = fake.capture_count;
    CHECK(first > 0);
    run(300, 0.1f, 0, 0, 0);
    still(SEC(35), 0, 0);
    CHECK_INT(fake.capture_count, first);   /* Inside the 60 s limit. */
    still(SEC(30), 0, 0);
    run(300, 0.1f, 0, 0, 0);
    still(SEC(35), 0, 0);
    CHECK(fake.capture_count > first);
}

static void capture_now_and_simulated_motion_work_in_debug(void) {
    setup(true);
    s.debug = true;
    yg_engine_setting_changed(e, now, "debug", "1");
    still(SEC(3), 0, 0);
    CHECK(yg_engine_capture_now(e, now));
    still(SEC(35), 0, 0);
    CHECK(fake.capture_count > 0);
    CHECK(yg_engine_simulate_motion(e, now));
    yg_engine_on_advert(e, now, &GATE, -85, &ib);
    CHECK_INT(fake.presses, 1);
}

static void debug_snapshot_runs_once_when_parked(void) {
    setup(true);
    s.debug = true;
    yg_engine_setting_changed(e, now, "debug", "1");
    still(SEC(9), 0, 0);
    CHECK_INT(fake.env_starts, 0);
    still(SEC(2), 0, 0);
    CHECK_INT(fake.env_starts, 1);
    CHECK_INT(fake.scan, YG_SCAN_OPEN);
    still(SEC(4), 0, 0);
    CHECK_INT(fake.env_finishes, 0);
    still(SEC(3), 0, 0);
    CHECK_INT(fake.env_finishes, 1);
    CHECK_INT(fake.scan, YG_SCAN_FILTERED);
    CHECK_INT(count_records(YG_REC_ENV), 1);
    still(SEC(60), 0, 0);
    CHECK_INT(fake.env_starts, 1);
}

static void debug_snapshot_cancels_when_the_car_moves(void) {
    setup(true);
    s.debug = true;
    yg_engine_setting_changed(e, now, "debug", "1");
    still(SEC(11), 0, 0);
    CHECK_INT(fake.env_starts, 1);
    pull_out(SEC(2), 0, 0);
    CHECK_INT(fake.scan, YG_SCAN_FILTERED);
    CHECK_INT(count_records(YG_REC_ENV), 0);
}

static void debug_snapshot_skips_when_already_moved(void) {
    setup(true);
    s.debug = true;
    yg_engine_setting_changed(e, now, "debug", "1");
    still(SEC(3), 0, 0);
    pull_out(SEC(3), 0, 0);
    still(SEC(10), 0, 0);
    CHECK_INT(fake.env_starts, 0);
}

static void paired_adverts_still_count_during_the_open_scan(void) {
    setup(true);
    s.debug = true;
    yg_engine_setting_changed(e, now, "debug", "1");
    still(SEC(11), 0, 0);
    CHECK_INT(fake.scan, YG_SCAN_OPEN);
    pull_out(SEC(2), 300, -85);
    CHECK_INT(fake.presses, 1);
}

static void rating_and_time_anchor_are_logged(void) {
    setup(true);
    CHECK(yg_engine_rate_trip(e, now, 5, "too_early"));
    CHECK(!yg_engine_rate_trip(e, now, 5, "whatever"));
    CHECK_INT(count_records(YG_REC_RATING), 1);
    yg_engine_time_anchor(e, now, 1790000000u);
    CHECK_INT(count_records(YG_REC_TIME_ANCHOR), 1);
}

static void status_reports_the_live_state(void) {
    setup(true);
    still(SEC(3), 300, -85);
    static char buf[1536];
    int n = yg_engine_status_json(e, now, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK_HAS(buf, "\"fw\":\"2.0.0\"");
    CHECK_HAS(buf, "\"trip\":7");
    CHECK_HAS(buf, "\"state\":\"armed\"");
    CHECK_HAS(buf, "\"beacon\":\"c5:b0:e6:53:59:02\"");
    CHECK_HAS(buf, "\"rssi\":-85");
    CHECK_HAS(buf, "\"set_point\":\"any\"");
    CHECK_HAS(buf, "\"moved_recently\":false");
    CHECK_HAS(buf, "\"sensor\":\"ok\"");
    CHECK_HAS(buf, "\"sensor_id\":104");
    CHECK_HAS(buf, "\"temp_c\":33.5");
    CHECK_HAS(buf, "\"hotspot_on\":false");
    CHECK_HAS(buf, "\"pair_mode\":\"idle\"");
    CHECK_HAS(buf, "\"debug\":false");
    CHECK_HAS(buf, "\"fallback\":false");
    CHECK_HAS(buf, "\"adverts_per_s\":");
    CHECK_HAS(buf, "\"pair_saves\":0");
    yg_engine_pair_set(e, now, GATE.addr, GATE.type);
    CHECK(yg_engine_status_json(e, now, buf, sizeof(buf)) > 0);
    CHECK_HAS(buf, "\"pair_saves\":1");
}

static void debug_view_shows_raw_sensor_values(void) {
    setup(true);
    still(SEC(3), 0, 0);
    static char buf[512];
    CHECK(yg_engine_debug_json(e, now, buf, sizeof(buf)) > 0);
    CHECK_HAS(buf, "\"az_mg\":1000");
    CHECK_HAS(buf, "\"gravity\":[");
    CHECK_HAS(buf, "\"bias\":[");
    CHECK_HAS(buf, "\"near_miss\":false");
}

int main(void) {
    RUN(debug_view_shows_raw_sensor_values);
    RUN(boot_is_logged_and_scanning_starts);
    RUN(owners_failure_ignition_in_spot_never_presses_and_pull_out_presses_once);
    RUN(weak_day_presses_when_the_beacon_is_first_heard_on_the_way);
    RUN(arriving_from_the_street_presses_on_first_advert);
    RUN(stopped_at_a_closed_gate_still_presses);
    RUN(after_a_press_quiet_until_still_for_two_minutes);
    RUN(after_a_press_rearms_once_the_beacon_is_gone);
    RUN(next_start_begins_fresh);
    RUN(set_point_stops_a_press_from_driving_past);
    RUN(other_devices_never_count_as_the_beacon);
    RUN(sensor_failure_falls_back_to_the_threshold);
    RUN(absent_sensor_falls_back_at_boot);
    RUN(short_sensor_dropouts_never_press_a_parked_car);
    RUN(short_outages_are_logged_with_their_length);
    RUN(a_sleeping_sensor_presses_nothing);
    RUN(a_sensor_asleep_at_power_on_does_not_fall_back);
    RUN(a_sensor_that_stays_asleep_counts_as_failed);
    RUN(the_driver_leaving_after_a_fallback_press_still_presses);
    RUN(a_fallback_press_stays_single_after_the_sensor_recovers);
    RUN(the_motion_estimate_starts_over_after_an_outage);
    RUN(set_point_here_forgets_the_old_beacon);
    RUN(sensor_recovery_is_logged_and_restores_the_rule);
    RUN(unpaired_never_auto_presses_and_does_not_scan);
    RUN(pairing_by_holding_close_saves_and_returns_to_filtered_scan);
    RUN(pairing_timeout_keeps_the_old_beacon);
    RUN(pairing_suspends_presses_but_manual_still_works);
    RUN(typed_mac_saves_with_any_type);
    RUN(forgetting_stops_presses_and_scanning);
    RUN(set_point_here_uses_the_strongest_recent_advert);
    RUN(reset_to_defaults_restores_every_setting);
    RUN(reset_to_defaults_keeps_the_beacon_and_presses_nothing);
    RUN(reset_to_defaults_logs_only_what_changed);
    RUN(reset_to_defaults_answers_with_success);
    RUN(reset_to_defaults_ends_debug_captures);
    RUN(restarting_the_trip_count_makes_this_trip_number_one);
    RUN(restarting_the_trip_count_changes_nothing_else);
    RUN(scanning_has_the_whole_radio_with_the_hotspot_off);
    RUN(scanning_shares_the_radio_while_the_hotspot_is_on);
    RUN(a_scan_for_pairing_shares_the_radio_too);
    RUN(the_hotspot_does_not_start_a_scan_with_no_beacon);
    RUN(the_hotspot_keeps_a_debug_snapshot_scanning);
    RUN(set_here_looks_further_back_while_sharing_the_radio);
    RUN(the_gate_still_opens_while_sharing_the_radio);
    RUN(set_point_by_value_and_any);
    RUN(automatic_presses_are_silent);
    RUN(fallback_presses_are_silent);
    RUN(presses_by_hand_beep);
    RUN(asking_for_the_hotspot_answers_with_sound_and_light);
    RUN(holding_the_button_answers_that_pairing_started);
    RUN(holding_the_button_15_s_sounds_and_then_erases);
    RUN(a_factory_reset_does_not_depend_on_the_buzzer_or_led);
    RUN(pairing_results_have_their_own_sounds);
    RUN(starting_with_no_beacon_sounds_only_then);
    RUN(a_failed_sensor_sounds_once_per_trip);
    RUN(a_sensor_turned_off_on_purpose_stays_quiet);
    RUN(turning_the_buzzer_off_silences_everything);
    RUN(status_holds_motion_peaks_for_the_live_graph);
    RUN(led_off_suppresses_led_events);
    RUN(press_lights_the_led_and_it_goes_out_after);
    RUN(every_gesture_does_its_job);
    RUN(hotspot_stays_off_until_someone_asks);
    RUN(a_timed_hotspot_starts_on_and_stops_when_the_car_moves);
    RUN(hotspot_stays_while_a_client_is_connected);
    RUN(first_heard_logs_once_and_motion_logs_are_limited);
    RUN(temperature_logs_once_a_minute);
    RUN(traces_reach_the_log);
    RUN(debug_captures_around_motion_and_press);
    RUN(no_captures_without_debug);
    RUN(debug_near_misses_capture_at_a_limited_rate);
    RUN(capture_now_and_simulated_motion_work_in_debug);
    RUN(debug_snapshot_runs_once_when_parked);
    RUN(debug_snapshot_cancels_when_the_car_moves);
    RUN(debug_snapshot_skips_when_already_moved);
    RUN(paired_adverts_still_count_during_the_open_scan);
    RUN(rating_and_time_anchor_are_logged);
    RUN(status_reports_the_live_state);
    return TEST_RESULT();
}
