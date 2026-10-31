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

static void setup(void) {
    yg_sensor_health_init(&h);
    now = 1000;
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

static void ten_read_failures_fail_but_nine_do_not(void) {
    setup();
    feed(1.0f, 30);
    for (int i = 0; i < 9; i++) yg_sensor_health_on_read_failed(&h);
    CHECK_INT(h.state, YG_SENSOR_OK);
    feed(1.0f, 1);
    for (int i = 0; i < 9; i++) yg_sensor_health_on_read_failed(&h);
    CHECK_INT(h.state, YG_SENSOR_OK);
    yg_sensor_health_on_read_failed(&h);
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
    for (int i = 0; i < 10; i++) yg_sensor_health_on_read_failed(&h);
    CHECK_INT(h.state, YG_SENSOR_FAILED);
    feed(1.0f, 249);
    CHECK_INT(h.state, YG_SENSOR_FAILED);
    feed(1.0f, 1);
    CHECK_INT(h.state, YG_SENSOR_OK);
    CHECK_INT(h.err, YG_SENSOR_ERR_NONE);
}

static void absent_sensor_stays_failed(void) {
    setup();
    yg_sensor_health_absent(&h);
    CHECK_INT(h.state, YG_SENSOR_FAILED);
    CHECK_INT(h.err, YG_SENSOR_ERR_ABSENT);
    feed(1.0f, 500);
    CHECK_INT(h.state, YG_SENSOR_FAILED);
}

int main(void) {
    RUN(good_start_becomes_ok);
    RUN(wrong_gravity_at_start_fails);
    RUN(ten_read_failures_fail_but_nine_do_not);
    RUN(identical_samples_for_five_seconds_fail);
    RUN(recovers_after_good_changing_samples);
    RUN(absent_sensor_stays_failed);
    return TEST_RESULT();
}
