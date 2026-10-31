#include "yg_motion.h"
#include "yg_record.h"
#include "yg_test.h"
#include "yg_trigger.h"

static yg_record_t r, back;
static uint8_t raw[YG_RECORD_MAX_ENCODED * 2];
static char json[YG_RECORD_JSON_MAX];

/* Encodes, decodes, and renders the record, leaving the JSON in `json`. */
static void round_trip(void) {
    memset(raw, 0xFF, sizeof(raw));
    size_t n = yg_record_encode(&r, raw);
    CHECK_INT(n % 4, 0);
    CHECK(n <= YG_RECORD_MAX_ENCODED);
    size_t pos = 0;
    CHECK(yg_record_next(raw, sizeof(raw), &pos, &back));
    CHECK_INT(pos, n);
    CHECK_INT(back.type, r.type);
    CHECK_INT(back.len, r.len);
    CHECK_INT(back.trip, r.trip);
    CHECK_INT(back.uptime_ms, r.uptime_ms);
    CHECK(memcmp(back.payload, r.payload, r.len) == 0);
    CHECK(yg_record_to_json(&back, json, sizeof(json)) > 0);
    CHECK(json[0] == '{');
    CHECK(json[strlen(json) - 1] == '}');
}

static void boot_record(void) {
    yg_settings_t s;
    yg_settings_defaults(&s);
    s.set_point_dbm = -85;
    s.debug = true;
    yg_beacon_t b = { true, { { 0xC5, 0xB0, 0xE6, 0x53, 0x59, 0x02 }, YG_ADDR_TYPE_RANDOM } };
    yg_rec_boot(&r, 12, 345, "2.0.0", 3, 0x68, &b, &s);
    round_trip();
    CHECK_HAS(json, "\"k\":\"boot\"");
    CHECK_HAS(json, "\"trip\":12");
    CHECK_HAS(json, "\"t\":345");
    CHECK_HAS(json, "\"fw\":\"2.0.0\"");
    CHECK_HAS(json, "\"reset\":3");
    CHECK_HAS(json, "\"sensor_id\":104");
    CHECK_HAS(json, "\"beacon\":\"c5:b0:e6:53:59:02\"");
    CHECK_HAS(json, "\"set_point\":-85");
    CHECK_HAS(json, "\"debug\":true");
    CHECK_HAS(json, "\"press_ms\":2000");
    CHECK_HAS(json, "\"moved_window_s\":60");
    CHECK_HAS(json, "\"motion_accel_mg\":40");

    b.paired = false;
    s.set_point_dbm = YG_SET_POINT_ANY;
    yg_rec_boot(&r, 12, 345, "2.0.0", 3, 0, &b, &s);
    round_trip();
    CHECK_HAS(json, "\"beacon\":null");
    CHECK_HAS(json, "\"set_point\":\"any\"");
}

static void small_records(void) {
    yg_rec_first_heard(&r, 1, 2, -91);
    round_trip();
    CHECK_HAS(json, "\"k\":\"first_heard\"");
    CHECK_HAS(json, "\"rssi\":-91");

    yg_rec_motion(&r, 1, 2, YG_MOTION_YAW, 44, -18.5f);
    round_trip();
    CHECK_HAS(json, "\"k\":\"motion\"");
    CHECK_HAS(json, "\"cause\":\"yaw\"");
    CHECK_HAS(json, "\"mg\":44");
    CHECK_HAS(json, "\"yaw\":-18.5");

    yg_rec_press(&r, 1, 2, YG_PRESS_AUTO, -85, 1200, YG_SET_POINT_ANY);
    round_trip();
    CHECK_HAS(json, "\"k\":\"press\"");
    CHECK_HAS(json, "\"reason\":\"auto\"");
    CHECK_HAS(json, "\"rssi\":-85");
    CHECK_HAS(json, "\"since_motion_ms\":1200");
    CHECK_HAS(json, "\"set_point\":\"any\"");

    yg_rec_press(&r, 1, 2, YG_PRESS_WEB, 0, 0, -70);
    round_trip();
    CHECK_HAS(json, "\"reason\":\"web\"");
    CHECK_HAS(json, "\"rssi\":null");
    CHECK_HAS(json, "\"set_point\":-70");

    yg_rec_rearm(&r, 1, 2, YG_REARM_STILL);
    round_trip();
    CHECK_HAS(json, "\"k\":\"rearm\"");
    CHECK_HAS(json, "\"reason\":\"still\"");

    yg_rec_hotspot(&r, 1, 2, true, 1);
    round_trip();
    CHECK_HAS(json, "\"k\":\"hotspot\"");
    CHECK_HAS(json, "\"on\":true");
    CHECK_HAS(json, "\"clients\":1");

    yg_addr_t a = { { 0xC5, 0xB0, 0xE6, 0x53, 0x59, 0x02 }, YG_ADDR_TYPE_RANDOM };
    yg_rec_pair(&r, 1, 2, YG_PAIR_EVENT_SAVED, &a);
    round_trip();
    CHECK_HAS(json, "\"k\":\"pair\"");
    CHECK_HAS(json, "\"event\":\"saved\"");
    CHECK_HAS(json, "\"mac\":\"c5:b0:e6:53:59:02\"");

    yg_rec_setting(&r, 1, 2, "press_ms", "1500");
    round_trip();
    CHECK_HAS(json, "\"k\":\"setting\"");
    CHECK_HAS(json, "\"key\":\"press_ms\"");
    CHECK_HAS(json, "\"value\":\"1500\"");

    yg_rec_ota(&r, 1, 2, YG_OTA_EVENT_CONFIRMED);
    round_trip();
    CHECK_HAS(json, "\"k\":\"ota\"");
    CHECK_HAS(json, "\"event\":\"confirmed\"");

    yg_rec_error(&r, 1, 2, YG_ERROR_SENSOR_STUCK, 9);
    round_trip();
    CHECK_HAS(json, "\"k\":\"error\"");
    CHECK_HAS(json, "\"code\":\"sensor_stuck\"");
    CHECK_HAS(json, "\"detail\":9");

    yg_rec_temp(&r, 1, 2, 31.5f);
    round_trip();
    CHECK_HAS(json, "\"k\":\"temp\"");
    CHECK_HAS(json, "\"c\":31.5");

    yg_rec_rating(&r, 9, 2, 7, YG_RATING_TOO_LATE);
    round_trip();
    CHECK_HAS(json, "\"k\":\"rating\"");
    CHECK_HAS(json, "\"rated_trip\":7");
    CHECK_HAS(json, "\"rating\":\"too_late\"");
    CHECK_INT(yg_rating_parse("too_late"), YG_RATING_TOO_LATE);
    CHECK_INT(yg_rating_parse("nonsense"), 0);

    yg_rec_time_anchor(&r, 1, 2, 1790000000u);
    round_trip();
    CHECK_HAS(json, "\"k\":\"time_anchor\"");
    CHECK_HAS(json, "\"epoch\":1790000000");

    yg_env_t env = { 14, 6, -48, { 1, 0, 0, 0, 0, 3, 0, 0, 0, 0, 2 } };
    yg_rec_env(&r, 1, 2, &env);
    round_trip();
    CHECK_HAS(json, "\"k\":\"env\"");
    CHECK_HAS(json, "\"ble\":14");
    CHECK_HAS(json, "\"wifi\":6");
    CHECK_HAS(json, "\"wifi_best\":-48");
    CHECK_HAS(json, "\"chan\":[1,0,0,0,0,3,0,0,0,0,2,0,0]");
}

static void trace_record(void) {
    yg_rec_trace_begin(&r, 5, 1000, 1);
    CHECK_INT(yg_rec_trace_count(&r), 0);
    yg_rec_trace_add(&r, -91, 3, 120, 7, 1);
    yg_rec_trace_add(&r, 0, 0, 5000, 400, 0);
    CHECK_INT(yg_rec_trace_count(&r), 2);
    round_trip();
    CHECK_HAS(json, "\"k\":\"trace\"");
    CHECK_HAS(json, "\"period\":1");
    CHECK_HAS(json, "\"s\":[[-91,3,120,7,1],[0,0,2550,255,0]]");
    for (int i = 2; i < YG_TRACE_MAX_SAMPLES; i++) yg_rec_trace_add(&r, -100, 255, 2550, 255, 255);
    CHECK_INT(yg_rec_trace_count(&r), YG_TRACE_MAX_SAMPLES);
    round_trip();
}

static void capture_records(void) {
    yg_rec_cap_start(&r, 1, 2, 77, YG_CAP_CAUSE_NEAR_MISS);
    round_trip();
    CHECK_HAS(json, "\"k\":\"cap_start\"");
    CHECK_HAS(json, "\"cap\":77");
    CHECK_HAS(json, "\"cause\":\"near_miss\"");

    int16_t raw_imu[6 * YG_CAP_IMU_PER_RECORD];
    for (int i = 0; i < 6 * YG_CAP_IMU_PER_RECORD; i++) raw_imu[i] = (int16_t)(i * 300 - 15000);
    yg_rec_cap_imu(&r, 1, 2, 77, 500, YG_CAP_IMU_PER_RECORD, raw_imu);
    CHECK(r.len <= YG_RECORD_MAX_PAYLOAD);
    round_trip();
    CHECK_HAS(json, "\"k\":\"cap_imu\"");
    CHECK_HAS(json, "\"first\":500");
    CHECK_HAS(json, "\"n\":19");
    CHECK_HAS(json, "\"b64\":\"");

    uint8_t entries[YG_CAP_ADV_ENTRY * 2] = { 1, 0, 0, 0, 0xA5, 2, 0, 0, 0, 0xB0 };
    yg_rec_cap_adv(&r, 1, 2, 77, 2, entries);
    round_trip();
    CHECK_HAS(json, "\"k\":\"cap_adv\"");
    CHECK_HAS(json, "\"n\":2");
    CHECK_HAS(json, "\"b64\":\"AQAAAKUCAAAAsA==\"");

    yg_rec_cap_end(&r, 1, 2, 77, 2000, 13);
    round_trip();
    CHECK_HAS(json, "\"k\":\"cap_end\"");
    CHECK_HAS(json, "\"samples\":2000");
    CHECK_HAS(json, "\"adverts\":13");
}

static void flipped_bit_is_rejected(void) {
    yg_rec_first_heard(&r, 1, 2, -91);
    memset(raw, 0xFF, sizeof(raw));
    size_t n = yg_record_encode(&r, raw);
    raw[5] ^= 0x10;
    size_t pos = 0;
    CHECK(!yg_record_next(raw, n, &pos, &back));
}

static void reader_skips_garbage_to_the_next_record(void) {
    memset(raw, 0xFF, sizeof(raw));
    yg_rec_first_heard(&r, 1, 2, -91);
    size_t n = yg_record_encode(&r, raw);
    raw[0] ^= 0x0F;   /* First record damaged. */
    yg_rec_rearm(&r, 1, 2, YG_REARM_GONE);
    yg_record_encode(&r, raw + n);
    size_t pos = 0;
    CHECK(yg_record_next(raw, sizeof(raw), &pos, &back));
    CHECK_INT(back.type, YG_REC_REARM);
    CHECK(!yg_record_next(raw, sizeof(raw), &pos, &back));
}

static void small_buffer_reports_overflow(void) {
    yg_rec_first_heard(&r, 1, 2, -91);
    char tiny[8];
    CHECK_INT(yg_record_to_json(&r, tiny, sizeof(tiny)), -1);
}

int main(void) {
    RUN(boot_record);
    RUN(small_records);
    RUN(trace_record);
    RUN(capture_records);
    RUN(flipped_bit_is_rejected);
    RUN(reader_skips_garbage_to_the_next_record);
    RUN(small_buffer_reports_overflow);
    return TEST_RESULT();
}
