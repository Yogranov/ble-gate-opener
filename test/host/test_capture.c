#include <stdlib.h>

#include "yg_capture.h"
#include "yg_test.h"

static yg_capture_t *c;
static yg_imu_sample_t quiet = { 0, 0, 1.0f, 0, 0, 0 };

static void setup(void) {
    if (c == NULL) c = malloc(sizeof(*c));
    yg_capture_init(c);
}

static int drain(uint32_t now, uint32_t trip, int *starts, int *imu_samples, int *adverts, int *ends) {
    yg_record_t r;
    int n = 0;
    while (yg_capture_next(c, now, trip, &r)) {
        n++;
        if (r.type == YG_REC_CAP_START && starts) (*starts)++;
        if (r.type == YG_REC_CAP_IMU && imu_samples) *imu_samples += r.payload[8];
        if (r.type == YG_REC_CAP_ADV && adverts) *adverts += r.payload[4];
        if (r.type == YG_REC_CAP_END && ends) (*ends)++;
        CHECK(n < 2000);
        if (n >= 2000) break;
    }
    return n;
}

static void nothing_is_emitted_without_a_trigger(void) {
    setup();
    for (int i = 0; i < 100; i++) yg_capture_on_imu(c, &quiet);
    CHECK_INT(drain(1000, 1, NULL, NULL, NULL, NULL), 0);
    CHECK(!yg_capture_active(c));
}

static void capture_spans_ten_seconds_before_and_thirty_after(void) {
    setup();
    uint32_t now = 0;
    for (int i = 0; i < 2000; i++) {   /* 40 s of history */
        yg_capture_on_imu(c, &quiet);
        now += 20;
    }
    int starts = 0, samples = 0, adverts = 0, ends = 0;
    CHECK(yg_capture_trigger(c, now, 5, YG_CAP_CAUSE_MOTION));
    CHECK(yg_capture_active(c));
    for (int i = 0; i < 1500; i++) {   /* 30 s after */
        yg_capture_on_imu(c, &quiet);
        if (i % 100 == 0) yg_capture_on_advert(c, now, -80);
        now += 20;
        drain(now, 5, &starts, &samples, &adverts, &ends);
    }
    now += 100;
    drain(now, 5, &starts, &samples, &adverts, &ends);
    CHECK_INT(starts, 1);
    CHECK_INT(ends, 1);
    CHECK_INT(adverts, 15);
    CHECK(samples >= 1980 && samples <= 2010);
    CHECK(!yg_capture_active(c));
}

static void early_trigger_takes_what_history_exists(void) {
    setup();
    for (int i = 0; i < 100; i++) yg_capture_on_imu(c, &quiet);
    int samples = 0, ends = 0;
    CHECK(yg_capture_trigger(c, 2000, 5, YG_CAP_CAUSE_PRESS));
    drain(2000 + YG_CAP_POST_MS + 100, 5, NULL, &samples, NULL, &ends);
    CHECK_INT(samples, 100);
    CHECK_INT(ends, 1);
}

static void second_trigger_during_a_capture_is_ignored(void) {
    setup();
    CHECK(yg_capture_trigger(c, 1000, 5, YG_CAP_CAUSE_MOTION));
    CHECK(!yg_capture_trigger(c, 2000, 5, YG_CAP_CAUSE_PRESS));
    int ends = 0;
    drain(1000 + YG_CAP_POST_MS + 100, 5, NULL, NULL, NULL, &ends);
    CHECK_INT(ends, 1);
    CHECK(yg_capture_trigger(c, 1000 + YG_CAP_POST_MS + 200, 5, YG_CAP_CAUSE_PRESS));
}

static void old_adverts_before_the_window_are_left_out(void) {
    setup();
    yg_capture_on_advert(c, 1000, -70);
    yg_capture_on_advert(c, 25000, -71);
    yg_capture_on_advert(c, 26000, -72);
    int adverts = 0;
    CHECK(yg_capture_trigger(c, 30000, 5, YG_CAP_CAUSE_MANUAL));
    drain(30000 + YG_CAP_POST_MS + 100, 5, NULL, NULL, &adverts, NULL);
    CHECK_INT(adverts, 2);
}

static void falling_behind_skips_overwritten_samples(void) {
    setup();
    CHECK(yg_capture_trigger(c, 0, 5, YG_CAP_CAUSE_MOTION));
    for (int i = 0; i < YG_CAP_IMU_RING * 3; i++) yg_capture_on_imu(c, &quiet);
    int samples = 0;
    drain(YG_CAP_POST_MS + 100, 5, NULL, &samples, NULL, NULL);
    CHECK(samples <= YG_CAP_IMU_RING);
    CHECK(samples > 0);
}

static void raw_samples_use_the_documented_scale(void) {
    setup();
    yg_imu_sample_t s = { 0.5f, -0.25f, 1.0f, 10.0f, -20.0f, 0 };
    yg_capture_on_imu(c, &s);
    CHECK(yg_capture_trigger(c, 1000, 5, YG_CAP_CAUSE_MANUAL));
    yg_record_t r;
    CHECK(yg_capture_next(c, 1000 + YG_CAP_POST_MS + 100, 5, &r));
    CHECK_INT(r.type, YG_REC_CAP_START);
    CHECK(yg_capture_next(c, 1000 + YG_CAP_POST_MS + 100, 5, &r));
    CHECK_INT(r.type, YG_REC_CAP_IMU);
    CHECK_INT((int16_t)yg_get_u16(r.payload + 9), 8192);
    CHECK_INT((int16_t)yg_get_u16(r.payload + 11), -4096);
    CHECK_INT((int16_t)yg_get_u16(r.payload + 13), 16384);
    CHECK_INT((int16_t)yg_get_u16(r.payload + 15), 1310);
    CHECK_INT((int16_t)yg_get_u16(r.payload + 17), -2620);
}

int main(void) {
    RUN(nothing_is_emitted_without_a_trigger);
    RUN(capture_spans_ten_seconds_before_and_thirty_after);
    RUN(early_trigger_takes_what_history_exists);
    RUN(second_trigger_during_a_capture_is_ignored);
    RUN(old_adverts_before_the_window_are_left_out);
    RUN(falling_behind_skips_overwritten_samples);
    RUN(raw_samples_use_the_documented_scale);
    return TEST_RESULT();
}
