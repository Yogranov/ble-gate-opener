#include "yg_hotspot_policy.h"
#include "yg_test.h"

static yg_hotspot_policy_t h;
static yg_settings_t s;

/* A hotspot on a timer, which anyone can still choose with the Hotspot time setting. */
static void setup(void) {
    yg_settings_defaults(&s);
    s.hotspot_s = 300;
    yg_hotspot_init(&h, &s, 0, false);
}

static void by_default_it_stays_off_until_requested(void) {
    yg_settings_defaults(&s);
    yg_hotspot_init(&h, &s, 0, false);
    CHECK(!yg_hotspot_tick(&h, &s, 1000));
    yg_hotspot_on_motion(&h);
    CHECK(!yg_hotspot_tick(&h, &s, 60000));
    yg_hotspot_request(&h, 70000);
    CHECK(yg_hotspot_tick(&h, &s, 71000));
    yg_hotspot_on_motion(&h);
    CHECK(yg_hotspot_tick(&h, &s, 72000));
    CHECK_INT(yg_hotspot_left_s(&h, &s, 72000), 298);
    CHECK(!yg_hotspot_tick(&h, &s, 370000));
}

static void on_at_start_and_off_when_the_car_moves(void) {
    setup();
    CHECK(yg_hotspot_tick(&h, &s, 1000));
    yg_hotspot_on_motion(&h);
    CHECK(!yg_hotspot_tick(&h, &s, 1100));
}

static void off_when_the_timer_expires(void) {
    setup();
    CHECK(yg_hotspot_tick(&h, &s, 299000));
    CHECK_INT(yg_hotspot_left_s(&h, &s, 299000), 1);
    CHECK(!yg_hotspot_tick(&h, &s, 300000));
    CHECK_INT(yg_hotspot_left_s(&h, &s, 300000), 0);
}

static void client_keeps_it_on_then_it_stops_30s_after_leaving(void) {
    setup();
    yg_hotspot_set_clients(&h, 1000, 1);
    yg_hotspot_on_motion(&h);
    CHECK(yg_hotspot_tick(&h, &s, 2000));
    CHECK(yg_hotspot_tick(&h, &s, 400000));
    yg_hotspot_set_clients(&h, 500000, 0);
    CHECK(yg_hotspot_tick(&h, &s, 529000));
    CHECK(!yg_hotspot_tick(&h, &s, 530000));
}

static void client_that_left_long_ago_does_not_delay_the_stop(void) {
    setup();
    yg_hotspot_set_clients(&h, 1000, 1);
    yg_hotspot_set_clients(&h, 2000, 0);
    CHECK(yg_hotspot_tick(&h, &s, 100000));
    yg_hotspot_on_motion(&h);
    CHECK(!yg_hotspot_tick(&h, &s, 100100));
}

static void zero_time_stays_off_until_requested(void) {
    yg_settings_defaults(&s);
    s.hotspot_s = 0;
    yg_hotspot_init(&h, &s, 0, false);
    CHECK(!yg_hotspot_tick(&h, &s, 1000));
    yg_hotspot_request(&h, 5000);
    CHECK(yg_hotspot_tick(&h, &s, 6000));
    CHECK(yg_hotspot_tick(&h, &s, 304000));
    CHECK(!yg_hotspot_tick(&h, &s, 305000));
}

static void debug_ignores_motion_but_not_the_timer(void) {
    setup();
    s.debug = true;
    yg_hotspot_on_motion(&h);
    CHECK(yg_hotspot_tick(&h, &s, 1000));
    CHECK(!yg_hotspot_tick(&h, &s, 300000));
}

static void once_off_it_stays_off_until_requested(void) {
    setup();
    yg_hotspot_on_motion(&h);
    CHECK(!yg_hotspot_tick(&h, &s, 1000));
    CHECK(!yg_hotspot_tick(&h, &s, 2000));
    yg_hotspot_set_clients(&h, 2100, 0);
    CHECK(!yg_hotspot_tick(&h, &s, 3000));
    yg_hotspot_request(&h, 4000);
    CHECK(yg_hotspot_tick(&h, &s, 5000));
}

static void requested_hotspot_ignores_motion_and_uses_the_timer(void) {
    setup();
    yg_hotspot_on_motion(&h);
    yg_hotspot_tick(&h, &s, 1000);
    yg_hotspot_request(&h, 10000);
    yg_hotspot_on_motion(&h);
    CHECK(yg_hotspot_tick(&h, &s, 11000));
    CHECK(yg_hotspot_tick(&h, &s, 309000));
    CHECK(!yg_hotspot_tick(&h, &s, 310000));
}

static void forced_on_after_update_even_when_disabled(void) {
    yg_settings_defaults(&s);
    s.hotspot_s = 0;
    yg_hotspot_init(&h, &s, 0, true);
    CHECK(yg_hotspot_tick(&h, &s, 1000));
    yg_hotspot_on_motion(&h);
    CHECK(yg_hotspot_tick(&h, &s, 2000));
    CHECK(!yg_hotspot_tick(&h, &s, 300000));
}

int main(void) {
    RUN(by_default_it_stays_off_until_requested);
    RUN(on_at_start_and_off_when_the_car_moves);
    RUN(off_when_the_timer_expires);
    RUN(client_keeps_it_on_then_it_stops_30s_after_leaving);
    RUN(client_that_left_long_ago_does_not_delay_the_stop);
    RUN(zero_time_stays_off_until_requested);
    RUN(debug_ignores_motion_but_not_the_timer);
    RUN(once_off_it_stays_off_until_requested);
    RUN(requested_hotspot_ignores_motion_and_uses_the_timer);
    RUN(forced_on_after_update_even_when_disabled);
    return TEST_RESULT();
}
