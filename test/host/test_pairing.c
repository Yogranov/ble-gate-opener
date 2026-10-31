#include "yg_pairing.h"
#include "yg_test.h"

static const uint8_t FRAME[] = {
    0x02, 0x01, 0x06, 0x1A, 0xFF, 0x4C, 0x00, 0x02, 0x15,
    0xFD, 0xA5, 0x06, 0x93, 0xA4, 0xE2, 0x4F, 0xB1, 0xAF, 0xCF, 0xC6, 0xEB, 0x07, 0x64, 0x78, 0x25,
    0x00, 0x01, 0x00, 0x02, 0xC5,
};

static yg_pairing_t p;
static yg_settings_t s;
static yg_ibeacon_t ib;
static const yg_addr_t A = { { 0xC5, 0xB0, 0xE6, 0x53, 0x59, 0x02 }, YG_ADDR_TYPE_RANDOM };
static const yg_addr_t B = { { 0xD1, 0x00, 0x00, 0x00, 0x00, 0x02 }, YG_ADDR_TYPE_RANDOM };
static const yg_addr_t ROTATING = { { 0x45, 0x00, 0x00, 0x00, 0x00, 0x03 }, YG_ADDR_TYPE_RANDOM };

static void parses_a_real_ibeacon_frame(void) {
    yg_ibeacon_t out;
    CHECK(yg_ibeacon_parse(FRAME, sizeof(FRAME), &out));
    CHECK(out.valid);
    char uuid[37];
    yg_ibeacon_uuid_str(&out, uuid);
    CHECK_STR(uuid, "fda50693-a4e2-4fb1-afcf-c6eb07647825");
    CHECK_INT(out.major, 1);
    CHECK_INT(out.minor, 2);
    CHECK_INT(out.tx_power, -59);
}

static void rejects_frames_that_are_not_ibeacons(void) {
    yg_ibeacon_t out;
    uint8_t other[sizeof(FRAME)];
    memcpy(other, FRAME, sizeof(FRAME));
    other[5] = 0x4D;
    CHECK(!yg_ibeacon_parse(other, sizeof(other), &out));
    CHECK(!out.valid);
    memcpy(other, FRAME, sizeof(FRAME));
    other[7] = 0x03;
    CHECK(!yg_ibeacon_parse(other, sizeof(other), &out));
    CHECK(!yg_ibeacon_parse(FRAME, sizeof(FRAME) - 1, &out));
    CHECK(!yg_ibeacon_parse(FRAME, 3, &out));
    CHECK(!yg_ibeacon_parse(NULL, 0, &out));
    const uint8_t zero_len[] = { 0x00, 0xFF };
    CHECK(!yg_ibeacon_parse(zero_len, sizeof(zero_len), &out));
    const uint8_t overrun[] = { 0x40, 0xFF, 0x4C };
    CHECK(!yg_ibeacon_parse(overrun, sizeof(overrun), &out));
}

static void classifies_addresses(void) {
    CHECK(yg_addr_is_stable(&A));
    CHECK(!yg_addr_is_stable(&ROTATING));
    yg_addr_t pub = { { 0x45, 0, 0, 0, 0, 1 }, YG_ADDR_TYPE_PUBLIC };
    CHECK(yg_addr_is_stable(&pub));
    yg_addr_t typed = { { 0x45, 0, 0, 0, 0, 1 }, YG_ADDR_TYPE_ANY };
    CHECK(yg_addr_is_stable(&typed));

    uint8_t raw[6];
    CHECK(yg_addr_parse("c5:b0:e6:53:59:02", raw));
    CHECK(memcmp(raw, A.addr, 6) == 0);
    CHECK(yg_addr_parse("C5-B0-E6-53-59-02", raw));
    CHECK(memcmp(raw, A.addr, 6) == 0);
    CHECK(!yg_addr_parse("c5:b0:e6:53:59", raw));
    CHECK(!yg_addr_parse("c5:b0:e6:53:59:02:11", raw));
    CHECK(!yg_addr_parse("zz:b0:e6:53:59:02", raw));
    char text[18];
    yg_addr_format(A.addr, text);
    CHECK_STR(text, "c5:b0:e6:53:59:02");

    yg_addr_t any = A;
    any.type = YG_ADDR_TYPE_ANY;
    yg_addr_t other_type = A;
    other_type.type = YG_ADDR_TYPE_PUBLIC;
    CHECK(yg_addr_equal(&A, &any));
    CHECK(!yg_addr_equal(&A, &other_type));
    CHECK(!yg_addr_equal(&A, &B));
}

static void setup(yg_pair_mode_t mode) {
    yg_settings_defaults(&s);
    yg_ibeacon_parse(FRAME, sizeof(FRAME), &ib);
    yg_pairing_init(&p);
    yg_pairing_start(&p, mode, 1000);
}

/* Sends adverts every 200 ms for `ms`. Returns the first result other than NONE. */
static yg_pair_result_t hear(uint32_t *now, uint32_t ms, const yg_addr_t *a, int8_t rssi_a, const yg_addr_t *b, int8_t rssi_b, yg_addr_t *win) {
    for (uint32_t t = 0; t < ms; t += 200) {
        *now += 200;
        if (a) yg_pairing_on_advert(&p, *now, a, rssi_a, &ib);
        if (b) yg_pairing_on_advert(&p, *now, b, rssi_b, &ib);
        yg_pair_result_t r = yg_pairing_tick(&p, &s, *now, win);
        if (r != YG_PAIR_RESULT_NONE) return r;
    }
    return YG_PAIR_RESULT_NONE;
}

static void single_close_beacon_saves_after_the_hold_time(void) {
    setup(YG_PAIR_AUTO);
    uint32_t now = 1000;
    yg_addr_t win;
    CHECK_INT(hear(&now, 1800, &A, -40, NULL, 0, &win), YG_PAIR_RESULT_NONE);
    CHECK_INT(hear(&now, 800, &A, -40, NULL, 0, &win), YG_PAIR_RESULT_SAVED);
    CHECK(yg_addr_equal(&win, &A));
    CHECK_INT(p.mode, YG_PAIR_IDLE);
}

static void two_close_beacons_never_save(void) {
    setup(YG_PAIR_AUTO);
    uint32_t now = 1000;
    yg_addr_t win;
    CHECK_INT(hear(&now, 20000, &A, -40, &B, -44, &win), YG_PAIR_RESULT_NONE);
}

static void close_beacon_beats_a_distant_one(void) {
    setup(YG_PAIR_AUTO);
    uint32_t now = 1000;
    yg_addr_t win;
    CHECK_INT(hear(&now, 5000, &A, -40, &B, -80, &win), YG_PAIR_RESULT_SAVED);
    CHECK(yg_addr_equal(&win, &A));
}

static void weak_beacon_never_saves(void) {
    setup(YG_PAIR_AUTO);
    uint32_t now = 1000;
    yg_addr_t win;
    CHECK_INT(hear(&now, 20000, &A, -60, NULL, 0, &win), YG_PAIR_RESULT_NONE);
}

static void rotating_addresses_and_other_devices_are_ignored(void) {
    setup(YG_PAIR_AUTO);
    uint32_t now = 1000;
    yg_addr_t win;
    CHECK_INT(hear(&now, 20000, &ROTATING, -30, NULL, 0, &win), YG_PAIR_RESULT_NONE);
    for (int i = 0; i < 100; i++) {
        now += 200;
        yg_pairing_on_advert(&p, now, &B, -30, NULL);
        CHECK_INT(yg_pairing_tick(&p, &s, now, &win), YG_PAIR_RESULT_NONE);
    }
}

static void rotating_neighbour_does_not_block_a_close_beacon(void) {
    setup(YG_PAIR_AUTO);
    uint32_t now = 1000;
    yg_addr_t win;
    CHECK_INT(hear(&now, 5000, &A, -40, &ROTATING, -41, &win), YG_PAIR_RESULT_SAVED);
}

static void dropping_away_restarts_the_hold(void) {
    setup(YG_PAIR_AUTO);
    uint32_t now = 1000;
    yg_addr_t win;
    CHECK_INT(hear(&now, 1600, &A, -40, NULL, 0, &win), YG_PAIR_RESULT_NONE);
    CHECK_INT(hear(&now, 2000, &A, -70, NULL, 0, &win), YG_PAIR_RESULT_NONE);
    CHECK_INT(hear(&now, 1600, &A, -30, NULL, 0, &win), YG_PAIR_RESULT_NONE);
    CHECK_INT(hear(&now, 1200, &A, -30, NULL, 0, &win), YG_PAIR_RESULT_SAVED);
}

static void gives_up_after_the_timeout(void) {
    setup(YG_PAIR_AUTO);
    uint32_t now = 1000;
    yg_addr_t win;
    CHECK_INT(hear(&now, 59000, NULL, 0, NULL, 0, &win), YG_PAIR_RESULT_NONE);
    CHECK_INT(hear(&now, 2000, NULL, 0, NULL, 0, &win), YG_PAIR_RESULT_TIMEOUT);
    CHECK_INT(p.mode, YG_PAIR_IDLE);
    CHECK_INT(yg_pairing_tick(&p, &s, now + 1000, &win), YG_PAIR_RESULT_NONE);
}

static void cancel_stops_it(void) {
    setup(YG_PAIR_AUTO);
    yg_pairing_cancel(&p);
    CHECK_INT(p.mode, YG_PAIR_IDLE);
    uint32_t now = 1000;
    yg_addr_t win;
    CHECK_INT(hear(&now, 5000, &A, -30, NULL, 0, &win), YG_PAIR_RESULT_NONE);
}

static void list_mode_never_saves_and_sorts_by_signal(void) {
    setup(YG_PAIR_LIST);
    uint32_t now = 1000;
    yg_addr_t win;
    CHECK_INT(hear(&now, 5000, &A, -80, &B, -30, &win), YG_PAIR_RESULT_NONE);
    static char buf[4096];
    int n = yg_pairing_candidates_json(&p, &s, now, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK_HAS(buf, "\"mode\":\"list\"");
    CHECK_HAS(buf, "\"mac\":\"c5:b0:e6:53:59:02\"");
    CHECK_HAS(buf, "\"uuid\":\"fda50693-a4e2-4fb1-afcf-c6eb07647825\"");
    CHECK_HAS(buf, "\"major\":1");
    CHECK(strstr(buf, "d1:00") < strstr(buf, "c5:b0"));
}

static void stale_candidates_leave_the_list(void) {
    setup(YG_PAIR_LIST);
    uint32_t now = 1000;
    yg_addr_t win;
    hear(&now, 1000, &A, -60, NULL, 0, &win);
    hear(&now, 11000, &B, -60, NULL, 0, &win);
    static char buf[4096];
    yg_pairing_candidates_json(&p, &s, now, buf, sizeof(buf));
    CHECK_HAS(buf, "d1:00");
    CHECK_LACKS(buf, "c5:b0");
}

static void a_full_table_replaces_the_oldest(void) {
    setup(YG_PAIR_LIST);
    uint32_t now = 1000;
    for (int i = 0; i < YG_PAIR_MAX_CANDIDATES + 4; i++) {
        yg_addr_t a = { { 0xC0, 0, 0, 0, 0, (uint8_t)i }, YG_ADDR_TYPE_RANDOM };
        now += 100;
        yg_pairing_on_advert(&p, now, &a, -50, &ib);
    }
    static char buf[4096];
    yg_pairing_candidates_json(&p, &s, now, buf, sizeof(buf));
    CHECK_LACKS(buf, "c0:00:00:00:00:00");
    CHECK_HAS(buf, "c0:00:00:00:00:13");
}

static void only_addresses_that_stay_can_be_paired(void) {
    const yg_addr_t typed = { { 0x45, 0, 0, 0, 0, 1 }, YG_ADDR_TYPE_ANY };
    const yg_addr_t public_addr = { { 0x45, 0, 0, 0, 0, 1 }, YG_ADDR_TYPE_PUBLIC };
    const yg_addr_t strange = { { 0xC5, 0, 0, 0, 0, 1 }, 7 };
    CHECK(yg_addr_can_pair(&A));
    CHECK(yg_addr_can_pair(&typed));
    CHECK(yg_addr_can_pair(&public_addr));
    CHECK(!yg_addr_can_pair(&ROTATING));
    CHECK(!yg_addr_can_pair(&strange));
}

int main(void) {
    RUN(only_addresses_that_stay_can_be_paired);
    RUN(parses_a_real_ibeacon_frame);
    RUN(rejects_frames_that_are_not_ibeacons);
    RUN(classifies_addresses);
    RUN(single_close_beacon_saves_after_the_hold_time);
    RUN(two_close_beacons_never_save);
    RUN(close_beacon_beats_a_distant_one);
    RUN(weak_beacon_never_saves);
    RUN(rotating_addresses_and_other_devices_are_ignored);
    RUN(rotating_neighbour_does_not_block_a_close_beacon);
    RUN(dropping_away_restarts_the_hold);
    RUN(gives_up_after_the_timeout);
    RUN(cancel_stops_it);
    RUN(list_mode_never_saves_and_sorts_by_signal);
    RUN(stale_candidates_leave_the_list);
    RUN(a_full_table_replaces_the_oldest);
    return TEST_RESULT();
}
