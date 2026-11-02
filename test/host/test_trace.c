#include "yg_motion.h"
#include "yg_test.h"
#include "yg_trace.h"
#include "yg_trigger.h"
#include "yg_trips.h"

#define SEC(x) ((uint32_t)(x) * 1000u)

static yg_trace_t t;
static yg_record_t out;
static uint32_t samples;
static uint32_t samples_1s;
static uint32_t samples_30s;
static uint32_t records;
static uint8_t last_period;

/* Runs 100 ms ticks over a time range, counting the samples produced. */
static void run(uint32_t from, uint32_t to) {
    for (uint32_t now = from; now < to; now += 100) {
        if (yg_trace_tick(&t, now, 7, &out)) {
            records++;
            samples += yg_rec_trace_count(&out);
            last_period = out.payload[0];
            if (last_period == 1) samples_1s += yg_rec_trace_count(&out);
            if (last_period == 30) samples_30s += yg_rec_trace_count(&out);
            CHECK_INT(out.type, YG_REC_TRACE);
            CHECK_INT(out.trip, 7);
        }
    }
}

static void setup(void) {
    yg_trace_init(&t, 0);
    samples = 0;
    samples_1s = 0;
    samples_30s = 0;
    records = 0;
    last_period = 0;
}

static void one_per_second_for_the_first_two_minutes_with_no_beacon(void) {
    setup();
    run(0, SEC(100));
    CHECK(samples >= 90 && samples <= 100);
    CHECK_INT(last_period, 1);
}

static void no_samples_afterwards_with_no_beacon(void) {
    setup();
    run(0, SEC(125));
    uint32_t after_start = samples;
    run(SEC(125), SEC(600));
    CHECK_INT(samples, after_start);
}

static void one_per_30s_once_still_for_two_minutes_with_beacon(void) {
    setup();
    for (uint32_t s = 0; s < 120; s++) {
        yg_trace_on_advert(&t, SEC(s), -91);
        run(SEC(s), SEC(s + 1));
    }
    for (uint32_t s = 120; s < 420; s++) {
        yg_trace_on_advert(&t, SEC(s), -91);
        run(SEC(s), SEC(s + 1));
    }
    CHECK(samples_30s >= 8 && samples_30s <= 11);
    CHECK_INT(last_period, 30);
}

static void back_to_one_per_second_when_the_car_moves(void) {
    setup();
    for (uint32_t s = 0; s < 300; s++) {
        yg_trace_on_advert(&t, SEC(s), -91);
        run(SEC(s), SEC(s + 1));
    }
    uint32_t before = samples_1s;
    uint32_t at_motion = records;
    for (uint32_t s = 300; s < 330; s++) {
        yg_trace_on_motion(&t, SEC(s));
        yg_trace_on_advert(&t, SEC(s), -80);
        run(SEC(s), SEC(s + 1));
    }
    uint32_t extra = samples_1s - before;
    CHECK(extra >= 24 && extra <= 31);
    CHECK(records > at_motion);
    CHECK_INT(last_period, 1);
}

static void beacon_heard_late_in_a_trip_restarts_sampling(void) {
    setup();
    run(0, SEC(300));
    uint32_t before = samples;
    for (uint32_t s = 300; s < 320; s++) {
        yg_trace_on_motion(&t, SEC(s));
        yg_trace_on_advert(&t, SEC(s), -88);
        run(SEC(s), SEC(s + 1));
    }
    CHECK(samples - before >= 14);
}

static void sampling_is_the_start_of_a_trip_and_near_the_beacon(void) {
    setup();
    CHECK(yg_trace_is_sampling(&t, SEC(119)));
    CHECK(!yg_trace_is_sampling(&t, SEC(121)));
    yg_trace_on_advert(&t, SEC(200), -85);
    CHECK(yg_trace_is_sampling(&t, SEC(229)));
    CHECK(!yg_trace_is_sampling(&t, SEC(231)));
}

static void records_flush_within_five_seconds(void) {
    setup();
    run(0, SEC(5) + 200);
    CHECK_INT(records, 1);
    CHECK(samples >= 4 && samples <= 6);
}

static void sample_holds_the_strongest_values_of_its_period(void) {
    setup();
    yg_trace_on_advert(&t, 100, -90);
    yg_trace_on_advert(&t, 200, -70);
    yg_trace_on_advert(&t, 300, -80);
    yg_trace_on_peaks(&t, 50, 3);
    yg_trace_on_peaks(&t, 120, 2);
    yg_trace_on_motion(&t, 400);
    yg_trace_on_flags(&t, YG_TRACE_FLAG_TILTING);
    run(0, SEC(6));
    CHECK(samples >= 5);
    uint8_t *s = out.payload + 2;
    CHECK_INT((int8_t)s[0], -70);
    CHECK_INT(s[1], 3);
    CHECK_INT(s[2], 12);
    CHECK_INT(s[3], 3);
    CHECK_INT(s[4], YG_TRACE_FLAG_MOVED | YG_TRACE_FLAG_TILTING);
    CHECK_INT((int8_t)s[5], 0);
}

static void summary_of_a_scripted_trip(void) {
    yg_trip_summary_t s;
    yg_record_t r;
    yg_settings_t set;
    yg_settings_defaults(&set);
    yg_beacon_t b = { true, { { 0xC5, 0xB0, 0xE6, 0x53, 0x59, 0x02 }, YG_ADDR_TYPE_RANDOM } };
    yg_trip_summary_init(&s, 9);
    CHECK(!s.seen);

    yg_rec_boot(&r, 9, 100, "2.0.1", 3, 0x68, &b, &set);
    yg_trip_summary_add(&s, &r);
    yg_rec_temp(&r, 9, 200, 41.5f);
    yg_trip_summary_add(&s, &r);
    yg_rec_first_heard(&r, 9, 1000, -92);
    yg_trip_summary_add(&s, &r);

    yg_rec_trace_begin(&r, 9, 1000, 1);
    yg_rec_trace_add(&r, -92, 2, 0, 0, 0);
    yg_rec_trace_add(&r, -90, 3, 0, 0, 0);
    yg_trip_summary_add(&s, &r);

    yg_rec_motion(&r, 9, 30000, YG_MOTION_YAW, 12, 16.0f);
    yg_trip_summary_add(&s, &r);
    yg_rec_motion(&r, 9, 31000, YG_MOTION_ACCEL, 50, 2.0f);
    yg_trip_summary_add(&s, &r);
    yg_rec_press(&r, 9, 30500, YG_PRESS_AUTO, -85, 500, YG_SET_POINT_ANY);
    yg_trip_summary_add(&s, &r);

    yg_rec_trace_begin(&r, 9, 30000, 1);
    yg_rec_trace_add(&r, -85, 3, 90, 20, YG_TRACE_FLAG_MOVED);
    yg_rec_trace_add(&r, -70, 3, 90, 20, YG_TRACE_FLAG_MOVED);
    yg_rec_trace_add(&r, -60, 2, 90, 20, YG_TRACE_FLAG_MOVED);
    yg_rec_trace_add(&r, -75, 1, 90, 20, YG_TRACE_FLAG_MOVED);
    yg_trip_summary_add(&s, &r);

    yg_rec_error(&r, 9, 40000, YG_ERROR_SENSOR_READS, 0);
    yg_trip_summary_add(&s, &r);
    yg_rec_rating(&r, 9, 50000, 3, YG_RATING_GOOD);   /* Rates a different trip. */
    yg_trip_summary_add(&s, &r);
    yg_rec_rating(&r, 12, 60, 9, YG_RATING_TOO_LATE);
    yg_trip_summary_add(&s, &r);

    CHECK(s.seen);
    CHECK_STR(s.fw, "2.0.1");
    CHECK_INT(s.first_heard_rssi, -92);
    CHECK_INT(s.first_heard_ms, 1000);
    CHECK_INT(s.first_motion_ms, 30000);
    CHECK_INT(s.first_motion_cause, YG_MOTION_YAW);
    CHECK_INT(s.presses, 1);
    CHECK_INT(s.first_press_ms, 30500);
    CHECK_INT(s.first_press_reason, YG_PRESS_AUTO);
    CHECK_INT(s.parked_best, -90);
    CHECK_INT(s.peak_rssi, -60);
    CHECK_INT(s.peak_ms, 32000);
    CHECK_INT(s.adverts, 14);
    CHECK_INT(s.temp_x10, 415);
    CHECK_INT(s.errors, 1);
    CHECK_INT(s.rating, YG_RATING_TOO_LATE);
    CHECK_INT(s.duration_ms, 50000);

    static char buf[1024];
    CHECK(yg_trip_summary_to_json(&s, buf, sizeof(buf)) > 0);
    CHECK_HAS(buf, "\"trip\":9");
    CHECK_HAS(buf, "\"fw\":\"2.0.1\"");
    CHECK_HAS(buf, "\"first_heard\":{\"t\":1000,\"rssi\":-92}");
    CHECK_HAS(buf, "\"first_motion\":{\"t\":30000,\"cause\":\"yaw\"}");
    CHECK_HAS(buf, "\"first_press\":{\"t\":30500,\"reason\":\"auto\",\"rssi\":-85}");
    CHECK_HAS(buf, "\"presses\":1");
    CHECK_HAS(buf, "\"parked_best\":-90");
    CHECK_HAS(buf, "\"peak\":{\"t\":32000,\"rssi\":-60}");
    CHECK_HAS(buf, "\"warning_ms\":1500");
    CHECK_HAS(buf, "\"temp_c\":41.5");
    CHECK_HAS(buf, "\"rating\":\"too_late\"");
}

static void summary_of_an_empty_trip_uses_nulls(void) {
    yg_trip_summary_t s;
    yg_record_t r;
    yg_settings_t set;
    yg_settings_defaults(&set);
    yg_beacon_t b = { false, { { 0 }, 0 } };
    yg_trip_summary_init(&s, 4);
    yg_rec_boot(&r, 4, 100, "2.0.0", 1, 0, &b, &set);
    yg_trip_summary_add(&s, &r);
    static char buf[1024];
    CHECK(yg_trip_summary_to_json(&s, buf, sizeof(buf)) > 0);
    CHECK_HAS(buf, "\"first_heard\":null");
    CHECK_HAS(buf, "\"first_motion\":null");
    CHECK_HAS(buf, "\"first_press\":null");
    CHECK_HAS(buf, "\"parked_best\":null");
    CHECK_HAS(buf, "\"peak\":null");
    CHECK_HAS(buf, "\"warning_ms\":null");
    CHECK_HAS(buf, "\"temp_c\":null");
    CHECK_HAS(buf, "\"rating\":null");
}

int main(void) {
    RUN(one_per_second_for_the_first_two_minutes_with_no_beacon);
    RUN(sampling_is_the_start_of_a_trip_and_near_the_beacon);
    RUN(no_samples_afterwards_with_no_beacon);
    RUN(one_per_30s_once_still_for_two_minutes_with_beacon);
    RUN(back_to_one_per_second_when_the_car_moves);
    RUN(beacon_heard_late_in_a_trip_restarts_sampling);
    RUN(records_flush_within_five_seconds);
    RUN(sample_holds_the_strongest_values_of_its_period);
    RUN(summary_of_a_scripted_trip);
    RUN(summary_of_an_empty_trip_uses_nulls);
    return TEST_RESULT();
}
