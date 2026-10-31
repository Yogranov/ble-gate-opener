#include "yg_test.h"
#include "yg_trigger.h"

#define SEC(x) ((uint32_t)(x) * 1000u)

static yg_trigger_t t;
static yg_settings_t s;

static void setup(void) {
    yg_settings_defaults(&s);
    yg_trigger_init(&t, true);
}

/* Runs ticks every 100 ms from `from` up to and including `to`. Returns the
 * first non-NONE re-arm reason seen. */
static yg_rearm_reason_t run_ticks(uint32_t from, uint32_t to) {
    yg_rearm_reason_t seen = YG_REARM_NONE;
    for (uint32_t now = from; (int32_t)(to - now) >= 0; now += 100) {
        yg_rearm_reason_t r = yg_trigger_tick(&t, &s, now);
        if (seen == YG_REARM_NONE) seen = r;
    }
    return seen;
}

static void ignition_in_spot_with_beacon_heard_never_presses(void) {
    setup();
    for (uint32_t now = 0; now < SEC(300); now += 300) {
        CHECK_INT(yg_trigger_on_advert(&t, &s, now, -70), YG_PRESS_NONE);
        yg_trigger_tick(&t, &s, now);
    }
    CHECK_INT(t.state, YG_TRIG_ARMED);
}

static void pulling_out_with_beacon_heard_presses_once(void) {
    setup();
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(30), -91), YG_PRESS_NONE);
    yg_trigger_on_motion(&t, SEC(40));
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(40) + 300, -91), YG_PRESS_AUTO);
    CHECK_INT(t.state, YG_TRIG_PRESSING);
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(40) + 600, -80), YG_PRESS_NONE);
    run_ticks(SEC(41), SEC(50));
    CHECK_INT(t.state, YG_TRIG_HOLD);
    yg_trigger_on_motion(&t, SEC(50));
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(50) + 100, -60), YG_PRESS_NONE);
}

static void weak_day_presses_on_first_advert_after_motion(void) {
    setup();
    yg_trigger_on_motion(&t, SEC(10));
    run_ticks(SEC(10), SEC(13));
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(13), -93), YG_PRESS_AUTO);
}

static void arriving_from_street_presses_on_first_advert(void) {
    setup();
    for (uint32_t now = 0; now < SEC(600); now += SEC(5)) {
        yg_trigger_on_motion(&t, now);
        yg_trigger_tick(&t, &s, now);
    }
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(601), -92), YG_PRESS_AUTO);
}

static void stopped_at_closed_gate_still_presses_within_window(void) {
    setup();
    yg_trigger_on_motion(&t, SEC(100));
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(159), -90), YG_PRESS_AUTO);
}

static void no_press_when_motion_is_older_than_the_window(void) {
    setup();
    yg_trigger_on_motion(&t, SEC(100));
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(161), -90), YG_PRESS_NONE);
    CHECK(!yg_trigger_moved_recently(&t, &s, SEC(161)));
    CHECK(yg_trigger_moved_recently(&t, &s, SEC(160)));
}

static void press_ends_after_press_ms_then_holds(void) {
    setup();
    s.press_ms = 500;
    yg_trigger_on_motion(&t, SEC(1));
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(1), -80), YG_PRESS_AUTO);
    yg_trigger_tick(&t, &s, SEC(1) + 400);
    CHECK_INT(t.state, YG_TRIG_PRESSING);
    yg_trigger_tick(&t, &s, SEC(1) + 500);
    CHECK_INT(t.state, YG_TRIG_HOLD);
}

static void hold_rearms_after_beacon_gone(void) {
    setup();
    yg_trigger_on_motion(&t, SEC(10));
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(10), -80), YG_PRESS_AUTO);
    yg_trigger_tick(&t, &s, SEC(13));
    for (uint32_t now = SEC(14); now < SEC(69); now += SEC(1)) {
        yg_trigger_on_motion(&t, now);
        CHECK_INT(yg_trigger_tick(&t, &s, now), YG_REARM_NONE);
    }
    CHECK_INT(t.state, YG_TRIG_HOLD);
    yg_trigger_on_motion(&t, SEC(70));
    CHECK_INT(yg_trigger_tick(&t, &s, SEC(70)), YG_REARM_GONE);
    CHECK_INT(t.state, YG_TRIG_ARMED);
}

static void hold_rearms_after_car_still(void) {
    setup();
    yg_trigger_on_motion(&t, SEC(10));
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(10), -80), YG_PRESS_AUTO);
    yg_trigger_tick(&t, &s, SEC(13));
    for (uint32_t now = SEC(14); now < SEC(130); now += SEC(1)) {
        yg_trigger_on_advert(&t, &s, now, -60);
        CHECK_INT(yg_trigger_tick(&t, &s, now), YG_REARM_NONE);
    }
    yg_trigger_on_advert(&t, &s, SEC(130), -60);
    CHECK_INT(yg_trigger_tick(&t, &s, SEC(130)), YG_REARM_STILL);
    CHECK_INT(t.state, YG_TRIG_ARMED);
    /* Parked again: re-armed but not moved, so hearing the beacon does nothing. */
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(131), -60), YG_PRESS_NONE);
}

static void car_stopped_before_press_does_not_rearm_early(void) {
    /* Stopped 59 s at a closed gate, press, then drives through 70 s later. */
    setup();
    yg_trigger_on_motion(&t, SEC(100));
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(159), -85), YG_PRESS_AUTO);
    for (uint32_t now = SEC(160); now <= SEC(229); now += SEC(1)) {
        yg_trigger_on_advert(&t, &s, now, -70);
        CHECK_INT(yg_trigger_tick(&t, &s, now), YG_REARM_NONE);
    }
    yg_trigger_on_motion(&t, SEC(229));
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(229) + 100, -55), YG_PRESS_NONE);
    CHECK_INT(t.state, YG_TRIG_HOLD);
}

static void manual_press_with_no_beacon_does_not_rearm_early(void) {
    /* Weak day: pressed by hand, then the beacon is first heard on the way. */
    setup();
    CHECK_INT(yg_trigger_manual(&t, SEC(100), YG_PRESS_WEB), YG_PRESS_WEB);
    run_ticks(SEC(100), SEC(110));
    CHECK_INT(t.state, YG_TRIG_HOLD);
    yg_trigger_on_motion(&t, SEC(110));
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(115), -92), YG_PRESS_NONE);
}

static void ignition_off_after_press_starts_fresh(void) {
    setup();
    yg_trigger_on_motion(&t, SEC(1));
    yg_trigger_on_advert(&t, &s, SEC(1), -80);
    yg_trigger_init(&t, true);
    CHECK_INT(t.state, YG_TRIG_ARMED);
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(5), -60), YG_PRESS_NONE);
    CHECK(!yg_trigger_moved_recently(&t, &s, SEC(5)));
}

static void set_point_filters_weak_adverts(void) {
    setup();
    s.set_point_dbm = -85;
    yg_trigger_on_motion(&t, SEC(1));
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(2), -86), YG_PRESS_NONE);
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(3), -85), YG_PRESS_AUTO);
}

static void manual_press_works_from_every_idle_state(void) {
    setup();
    CHECK_INT(yg_trigger_manual(&t, SEC(1), YG_PRESS_BUTTON), YG_PRESS_BUTTON);
    CHECK_INT(t.state, YG_TRIG_PRESSING);
    CHECK_INT(yg_trigger_manual(&t, SEC(1) + 100, YG_PRESS_WEB), YG_PRESS_NONE);
    yg_trigger_tick(&t, &s, SEC(4));
    CHECK_INT(t.state, YG_TRIG_HOLD);
    CHECK_INT(yg_trigger_manual(&t, SEC(5), YG_PRESS_WEB), YG_PRESS_WEB);
}

static void unpaired_never_auto_presses_but_manual_works(void) {
    yg_settings_defaults(&s);
    yg_trigger_init(&t, false);
    CHECK_INT(t.state, YG_TRIG_UNPAIRED);
    yg_trigger_on_motion(&t, SEC(1));
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(1), -40), YG_PRESS_NONE);
    CHECK_INT(yg_trigger_manual(&t, SEC(2), YG_PRESS_WEB), YG_PRESS_WEB);
    yg_trigger_tick(&t, &s, SEC(5));
    CHECK_INT(t.state, YG_TRIG_UNPAIRED);
    yg_trigger_set_paired(&t, true);
    CHECK_INT(t.state, YG_TRIG_ARMED);
    yg_trigger_set_paired(&t, false);
    CHECK_INT(t.state, YG_TRIG_UNPAIRED);
}

static void pairing_suspends_auto_press_but_not_manual(void) {
    setup();
    yg_trigger_set_suspended(&t, true);
    yg_trigger_on_motion(&t, SEC(1));
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(1), -40), YG_PRESS_NONE);
    CHECK_INT(yg_trigger_manual(&t, SEC(2), YG_PRESS_BUTTON), YG_PRESS_BUTTON);
    run_ticks(SEC(2), SEC(6));
    yg_trigger_set_suspended(&t, false);
    CHECK_INT(t.state, YG_TRIG_HOLD);
}

static void sensor_failure_uses_the_fixed_threshold(void) {
    setup();
    yg_trigger_set_sensor_ok(&t, false);
    CHECK(yg_trigger_fallback_active(&t, &s));
    CHECK_INT(yg_trigger_effective_set_point(&t, &s), -90);
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(1), -91), YG_PRESS_NONE);
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(2), -90), YG_PRESS_FALLBACK);
}

static void motion_disabled_uses_the_fixed_threshold(void) {
    setup();
    s.motion_enabled = false;
    CHECK(yg_trigger_fallback_active(&t, &s));
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(2), -89), YG_PRESS_FALLBACK);
}

static void fallback_uses_the_stricter_set_point(void) {
    setup();
    yg_trigger_set_sensor_ok(&t, false);
    s.set_point_dbm = -70;
    CHECK_INT(yg_trigger_effective_set_point(&t, &s), -70);
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(1), -75), YG_PRESS_NONE);
    s.set_point_dbm = -95;
    CHECK_INT(yg_trigger_effective_set_point(&t, &s), -90);
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(1), -91), YG_PRESS_NONE);
}

static void fallback_hold_only_rearms_when_gone(void) {
    setup();
    yg_trigger_set_sensor_ok(&t, false);
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(1), -80), YG_PRESS_FALLBACK);
    for (uint32_t now = SEC(2); now <= SEC(500); now += SEC(1)) {
        CHECK_INT(yg_trigger_on_advert(&t, &s, now, -80), YG_PRESS_NONE);
        CHECK_INT(yg_trigger_tick(&t, &s, now), YG_REARM_NONE);
    }
    CHECK_INT(t.state, YG_TRIG_HOLD);
    CHECK_INT(yg_trigger_tick(&t, &s, SEC(561)), YG_REARM_GONE);
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(562), -80), YG_PRESS_FALLBACK);
}

static void timers_survive_clock_wrap(void) {
    setup();
    uint32_t base = UINT32_MAX - SEC(5);
    yg_trigger_on_motion(&t, base);
    CHECK(yg_trigger_moved_recently(&t, &s, base + SEC(30)));
    CHECK_INT(yg_trigger_on_advert(&t, &s, base + SEC(30), -80), YG_PRESS_AUTO);
    yg_trigger_tick(&t, &s, base + SEC(33));
    CHECK_INT(t.state, YG_TRIG_HOLD);
    CHECK_INT(yg_trigger_tick(&t, &s, base + SEC(89)), YG_REARM_NONE);
    CHECK_INT(yg_trigger_tick(&t, &s, base + SEC(90)), YG_REARM_GONE);
}

static void motion_from_before_a_press_does_not_count_after_it(void) {
    /* A long moved window must not make a parked car press again and again. */
    setup();
    s.moved_window_s = 300;
    s.still_s = 30;
    yg_trigger_on_motion(&t, SEC(10));
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(12), -70), YG_PRESS_AUTO);
    int presses = 0;
    for (uint32_t now = SEC(13); now < SEC(290); now += 300) {
        yg_trigger_tick(&t, &s, now);
        if (yg_trigger_on_advert(&t, &s, now, -70) != YG_PRESS_NONE) presses++;
    }
    CHECK_INT(presses, 0);
    CHECK_INT(t.state, YG_TRIG_ARMED);
    yg_trigger_on_motion(&t, SEC(290));
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(291), -70), YG_PRESS_AUTO);
}

static void motion_before_a_press_does_not_count_after_gone_either(void) {
    setup();
    s.moved_window_s = 300;
    yg_trigger_on_motion(&t, SEC(10));
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(12), -70), YG_PRESS_AUTO);
    yg_trigger_tick(&t, &s, SEC(15));
    CHECK_INT(yg_trigger_tick(&t, &s, SEC(72)), YG_REARM_GONE);
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(80), -70), YG_PRESS_NONE);
}

static void stillness_uses_up_motion_from_after_a_press(void) {
    /* Still rolling when it pressed, then stopped, with a long moved window. */
    setup();
    s.moved_window_s = 300;
    s.still_s = 30;
    yg_trigger_on_motion(&t, SEC(8));
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(9), -70), YG_PRESS_AUTO);
    yg_trigger_on_motion(&t, SEC(10));
    int presses = 0;
    for (uint32_t now = SEC(11); now < SEC(290); now += 300) {
        yg_trigger_tick(&t, &s, now);
        if (yg_trigger_on_advert(&t, &s, now, -70) != YG_PRESS_NONE) presses++;
    }
    CHECK_INT(presses, 0);
    CHECK(!yg_trigger_moved_recently(&t, &s, SEC(290)));
    yg_trigger_on_motion(&t, SEC(290));
    CHECK(yg_trigger_moved_recently(&t, &s, SEC(291)));
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(291), -70), YG_PRESS_AUTO);
}

static void the_beacon_going_uses_up_motion_too(void) {
    /* Pressed while rolling, then parked where the beacon comes and goes. */
    setup();
    s.moved_window_s = 300;
    s.gone_s = 10;
    yg_trigger_on_motion(&t, SEC(8));
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(9), -70), YG_PRESS_AUTO);
    yg_trigger_on_motion(&t, SEC(11));
    yg_trigger_tick(&t, &s, SEC(12));
    CHECK_INT(yg_trigger_tick(&t, &s, SEC(19)), YG_REARM_GONE);
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(25), -70), YG_PRESS_NONE);
    /* Moving again counts, so driving away and coming back still works. */
    yg_trigger_on_motion(&t, SEC(40));
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(41), -70), YG_PRESS_AUTO);
}

static void real_motion_ends_the_wait_after_a_fallback_press(void) {
    /* A sensor outage pressed while parked, in earshot. The driver leaves 5 minutes later. */
    setup();
    yg_trigger_set_sensor_ok(&t, false);
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(1), -80), YG_PRESS_FALLBACK);
    yg_trigger_set_sensor_ok(&t, true);
    for (uint32_t now = SEC(2); now <= SEC(300); now += SEC(1)) {
        yg_trigger_on_advert(&t, &s, now, -80);
        CHECK_INT(yg_trigger_tick(&t, &s, now), YG_REARM_NONE);
    }
    yg_trigger_on_motion(&t, SEC(301));
    CHECK_INT(yg_trigger_tick(&t, &s, SEC(301)), YG_REARM_MOVED);
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(301) + 100, -80), YG_PRESS_AUTO);
}

static void a_fallback_press_uses_up_the_motion_before_it(void) {
    /* The sensor fails just after the car stopped, with a long moved window. */
    setup();
    s.moved_window_s = 300;
    yg_trigger_on_motion(&t, SEC(1));
    yg_trigger_set_sensor_ok(&t, false);
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(7), -80), YG_PRESS_FALLBACK);
    yg_trigger_set_sensor_ok(&t, true);
    yg_trigger_tick(&t, &s, SEC(10));
    for (uint32_t now = SEC(11); now < SEC(290); now += SEC(1)) {
        yg_trigger_on_advert(&t, &s, now, -80);
        CHECK_INT(yg_trigger_tick(&t, &s, now), YG_REARM_NONE);
    }
}

static void motion_from_before_still_s_does_not_end_the_wait_later(void) {
    /* It moved a little after the press, and parked. Time passing is not motion. */
    setup();
    yg_trigger_set_sensor_ok(&t, false);
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(1), -80), YG_PRESS_FALLBACK);
    yg_trigger_set_sensor_ok(&t, true);
    yg_trigger_tick(&t, &s, SEC(5));
    yg_trigger_on_motion(&t, SEC(66));
    for (uint32_t now = SEC(67); now <= SEC(300); now += SEC(1)) {
        CHECK_INT(yg_trigger_on_advert(&t, &s, now, -80), YG_PRESS_NONE);
        CHECK_INT(yg_trigger_tick(&t, &s, now), YG_REARM_NONE);
    }
}

static void motion_right_after_a_fallback_press_does_not_press_again(void) {
    /* The gate has only just been told to open, by that press. */
    setup();
    yg_trigger_set_sensor_ok(&t, false);
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(1), -80), YG_PRESS_FALLBACK);
    yg_trigger_set_sensor_ok(&t, true);
    yg_trigger_tick(&t, &s, SEC(5));
    for (uint32_t now = SEC(6); now < SEC(121); now += SEC(1)) {
        yg_trigger_on_motion(&t, now);
        yg_trigger_on_advert(&t, &s, now, -80);
        CHECK_INT(yg_trigger_tick(&t, &s, now), YG_REARM_NONE);
    }
    CHECK_INT(t.state, YG_TRIG_HOLD);
}

static void fallback_press_waits_for_the_beacon_to_go_even_after_recovery(void) {
    setup();
    yg_trigger_set_sensor_ok(&t, false);
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(1), -80), YG_PRESS_FALLBACK);
    yg_trigger_set_sensor_ok(&t, true);
    for (uint32_t now = SEC(2); now <= SEC(500); now += SEC(1)) {
        yg_trigger_on_advert(&t, &s, now, -80);
        CHECK_INT(yg_trigger_tick(&t, &s, now), YG_REARM_NONE);
    }
    CHECK_INT(t.state, YG_TRIG_HOLD);
    CHECK_INT(yg_trigger_tick(&t, &s, SEC(561)), YG_REARM_GONE);
    CHECK_INT(yg_trigger_on_advert(&t, &s, SEC(562), -80), YG_PRESS_NONE);
}

int main(void) {
    RUN(motion_from_before_a_press_does_not_count_after_it);
    RUN(motion_before_a_press_does_not_count_after_gone_either);
    RUN(stillness_uses_up_motion_from_after_a_press);
    RUN(the_beacon_going_uses_up_motion_too);
    RUN(real_motion_ends_the_wait_after_a_fallback_press);
    RUN(a_fallback_press_uses_up_the_motion_before_it);
    RUN(motion_from_before_still_s_does_not_end_the_wait_later);
    RUN(motion_right_after_a_fallback_press_does_not_press_again);
    RUN(fallback_press_waits_for_the_beacon_to_go_even_after_recovery);
    RUN(ignition_in_spot_with_beacon_heard_never_presses);
    RUN(pulling_out_with_beacon_heard_presses_once);
    RUN(weak_day_presses_on_first_advert_after_motion);
    RUN(arriving_from_street_presses_on_first_advert);
    RUN(stopped_at_closed_gate_still_presses_within_window);
    RUN(no_press_when_motion_is_older_than_the_window);
    RUN(press_ends_after_press_ms_then_holds);
    RUN(hold_rearms_after_beacon_gone);
    RUN(hold_rearms_after_car_still);
    RUN(car_stopped_before_press_does_not_rearm_early);
    RUN(manual_press_with_no_beacon_does_not_rearm_early);
    RUN(ignition_off_after_press_starts_fresh);
    RUN(set_point_filters_weak_adverts);
    RUN(manual_press_works_from_every_idle_state);
    RUN(unpaired_never_auto_presses_but_manual_works);
    RUN(pairing_suspends_auto_press_but_not_manual);
    RUN(sensor_failure_uses_the_fixed_threshold);
    RUN(motion_disabled_uses_the_fixed_threshold);
    RUN(fallback_uses_the_stricter_set_point);
    RUN(fallback_hold_only_rearms_when_gone);
    RUN(timers_survive_clock_wrap);
    return TEST_RESULT();
}
