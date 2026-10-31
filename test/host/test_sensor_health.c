#include "yg_sensor_health.h"
#include "yg_test.h"

static yg_sensor_health_t h;
static uint32_t now;
static int counter;

static yg_imu_sample_t sample(float g) {
    counter++;
    yg_imu_sample_t s = { 0, 0, g + 0.0001f * (float)(counter % 7), 0.1f * (float)(counter % 3), 0, 0 };
    return s;
}

static void feed(float g, int n) {
    for (int i = 0; i < n; i++) {
        yg_imu_sample_t s = sample(g);
        yg_sensor_health_on_sample(&h, now, &s);
        now += 20;
    }
}

static void reads_fail(uint32_t ms, bool absent) {
    for (uint32_t t = 0; t < ms; t += 20) {
        yg_sensor_health_on_read_failed(&h, now, absent);
        now += 20;
    }
}

static void setup(void) {
    now = 1000;
    yg_sensor_health_init(&h, now);
}

static void good_start_becomes_ok(void) {
    setup();
    CHECK_INT(h.state, YG_SENSOR_STARTING);
    feed(1.0f, 30);
    CHECK_INT(h.state, YG_SENSOR_OK);
    CHECK_INT(h.err, YG_SENSOR_ERR_NONE);
}

static void wrong_gravity_at_start_fails(void) {
    setup();
    feed(0.5f, 30);
    CHECK_INT(h.state, YG_SENSOR_FAILED);
    CHECK_INT(h.err, YG_SENSOR_ERR_GRAVITY);
    setup();
    feed(1.5f, 30);
    CHECK_INT(h.err, YG_SENSOR_ERR_GRAVITY);
    setup();
    feed(0.75f, 30);
    CHECK_INT(h.state, YG_SENSOR_OK);
}

static void short_dropouts_change_nothing(void) {
    /* A loose wire must not hand control to the fixed threshold. */
    setup();
    feed(1.0f, 30);
    for (int i = 0; i < 5; i++) {
        reads_fail(4800, false);
        CHECK_INT(h.state, YG_SENSOR_OK);
        feed(1.0f, 1);
    }
    CHECK_INT(h.err, YG_SENSOR_ERR_NONE);
}

static void five_seconds_with_no_good_read_fails(void) {
    setup();
    feed(1.0f, 30);
    reads_fail(4900, false);
    CHECK_INT(h.state, YG_SENSOR_OK);
    reads_fail(200, false);
    CHECK_INT(h.state, YG_SENSOR_FAILED);
    CHECK_INT(h.err, YG_SENSOR_ERR_READS);
}

static void identical_samples_for_five_seconds_fail(void) {
    setup();
    feed(1.0f, 30);
    yg_imu_sample_t frozen = sample(1.0f);
    for (int i = 0; i < 240; i++) {
        yg_sensor_health_on_sample(&h, now, &frozen);
        now += 20;
    }
    CHECK_INT(h.state, YG_SENSOR_OK);
    for (int i = 0; i < 20; i++) {
        yg_sensor_health_on_sample(&h, now, &frozen);
        now += 20;
    }
    CHECK_INT(h.state, YG_SENSOR_FAILED);
    CHECK_INT(h.err, YG_SENSOR_ERR_STUCK);
}

static void recovers_after_good_changing_samples(void) {
    setup();
    feed(1.0f, 30);
    reads_fail(5100, false);
    CHECK_INT(h.state, YG_SENSOR_FAILED);
    feed(1.0f, 249);
    CHECK_INT(h.state, YG_SENSOR_FAILED);
    feed(1.0f, 1);
    CHECK_INT(h.state, YG_SENSOR_OK);
    CHECK_INT(h.err, YG_SENSOR_ERR_NONE);
}

static void missing_sensor_fails_after_five_seconds(void) {
    setup();
    reads_fail(4900, true);
    CHECK_INT(h.state, YG_SENSOR_STARTING);
    reads_fail(200, true);
    CHECK_INT(h.state, YG_SENSOR_FAILED);
    CHECK_INT(h.err, YG_SENSOR_ERR_ABSENT);
}

static void sensor_found_late_still_recovers(void) {
    setup();
    reads_fail(6000, true);
    CHECK_INT(h.err, YG_SENSOR_ERR_ABSENT);
    feed(1.0f, 250);
    CHECK_INT(h.state, YG_SENSOR_OK);
    CHECK_INT(h.err, YG_SENSOR_ERR_NONE);
}

static void sensor_found_soon_after_power_on_is_fine(void) {
    setup();
    reads_fail(1000, true);
    feed(1.0f, 30);
    CHECK_INT(h.state, YG_SENSOR_OK);
}

int main(void) {
    RUN(good_start_becomes_ok);
    RUN(wrong_gravity_at_start_fails);
    RUN(short_dropouts_change_nothing);
    RUN(five_seconds_with_no_good_read_fails);
    RUN(identical_samples_for_five_seconds_fail);
    RUN(recovers_after_good_changing_samples);
    RUN(missing_sensor_fails_after_five_seconds);
    RUN(sensor_found_late_still_recovers);
    RUN(sensor_found_soon_after_power_on_is_fine);
    return TEST_RESULT();
}
