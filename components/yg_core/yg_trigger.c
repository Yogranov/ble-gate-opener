#include "yg_trigger.h"

#include <string.h>

/* Millisecond clocks wrap, so all comparisons go through unsigned differences. */
static uint32_t elapsed(uint32_t now, uint32_t then) {
    return now - then;
}

static uint32_t later(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) > 0 ? a : b;
}

/* A press uses up the motion before it, and so does re-arming: otherwise a parked car
 * could press again, however the moved window and timers are set. */
static void start_press(yg_trigger_t *t, uint32_t now, bool until_gone) {
    t->state = YG_TRIG_PRESSING;
    t->motion_unused = false;
    t->until_gone = until_gone;
    t->press_time = now;
}

static void rearm(yg_trigger_t *t) {
    t->state = YG_TRIG_ARMED;
    t->motion_unused = false;
}

void yg_trigger_init(yg_trigger_t *t, bool paired) {
    memset(t, 0, sizeof(*t));
    t->paired = paired;
    t->sensor_ok = true;
    t->state = paired ? YG_TRIG_ARMED : YG_TRIG_UNPAIRED;
}

void yg_trigger_set_paired(yg_trigger_t *t, bool paired) {
    t->paired = paired;
    if (t->state == YG_TRIG_PRESSING) {
        return;
    }
    if (!paired) {
        t->state = YG_TRIG_UNPAIRED;
    } else if (t->state == YG_TRIG_UNPAIRED) {
        t->state = YG_TRIG_ARMED;
    }
}

void yg_trigger_set_suspended(yg_trigger_t *t, bool suspended) {
    t->suspended = suspended;
}

void yg_trigger_set_sensor_ok(yg_trigger_t *t, bool ok) {
    t->sensor_ok = ok;
}

void yg_trigger_on_motion(yg_trigger_t *t, uint32_t now) {
    t->has_motion = true;
    t->motion_unused = true;
    t->last_motion = now;
}

bool yg_trigger_fallback_active(const yg_trigger_t *t, const yg_settings_t *s) {
    return !t->sensor_ok || !s->motion_enabled;
}

int8_t yg_trigger_effective_set_point(const yg_trigger_t *t, const yg_settings_t *s) {
    if (!yg_trigger_fallback_active(t, s)) {
        return s->set_point_dbm;
    }
    if (s->set_point_dbm == YG_SET_POINT_ANY || s->set_point_dbm < s->fallback_dbm) {
        return s->fallback_dbm;
    }
    return s->set_point_dbm;
}

bool yg_trigger_moved_recently(const yg_trigger_t *t, const yg_settings_t *s, uint32_t now) {
    return t->motion_unused && elapsed(now, t->last_motion) <= (uint32_t)s->moved_window_s * 1000u;
}

yg_press_reason_t yg_trigger_on_advert(yg_trigger_t *t, const yg_settings_t *s, uint32_t now, int8_t rssi) {
    t->has_advert = true;
    t->last_advert = now;
    if (t->state != YG_TRIG_ARMED || t->suspended) {
        return YG_PRESS_NONE;
    }
    int8_t set_point = yg_trigger_effective_set_point(t, s);
    if (set_point != YG_SET_POINT_ANY && rssi < set_point) {
        return YG_PRESS_NONE;
    }
    if (yg_trigger_fallback_active(t, s)) {
        start_press(t, now, true);
        return YG_PRESS_FALLBACK;
    }
    if (!yg_trigger_moved_recently(t, s, now)) {
        return YG_PRESS_NONE;
    }
    start_press(t, now, false);
    return YG_PRESS_AUTO;
}

yg_press_reason_t yg_trigger_manual(yg_trigger_t *t, uint32_t now, yg_press_reason_t reason) {
    if (t->state == YG_TRIG_PRESSING) {
        return YG_PRESS_NONE;
    }
    start_press(t, now, false);
    return reason;
}

yg_rearm_reason_t yg_trigger_tick(yg_trigger_t *t, const yg_settings_t *s, uint32_t now) {
    if (t->state == YG_TRIG_PRESSING) {
        if (elapsed(now, t->press_time) < s->press_ms) {
            return YG_REARM_NONE;
        }
        t->state = t->paired ? YG_TRIG_HOLD : YG_TRIG_UNPAIRED;
    }
    if (t->state != YG_TRIG_HOLD) {
        return YG_REARM_NONE;
    }
    uint32_t heard = t->has_advert ? later(t->last_advert, t->press_time) : t->press_time;
    if (elapsed(now, heard) >= (uint32_t)s->gone_s * 1000u) {
        rearm(t);
        return YG_REARM_GONE;
    }
    if (yg_trigger_fallback_active(t, s)) {
        return YG_REARM_NONE;
    }
    uint32_t still_ms = (uint32_t)s->still_s * 1000u;
    if (t->until_gone) {
        /* Pressed with no motion to go on, so the car may sit in earshot for good. A sensor
         * that keeps dropping out must not press again, but a driver leaving must. */
        if (yg_trigger_moved_recently(t, s, now) && elapsed(now, t->press_time) >= still_ms) {
            t->state = YG_TRIG_ARMED;
            return YG_REARM_MOVED;
        }
        return YG_REARM_NONE;
    }
    uint32_t moved = t->has_motion ? later(t->last_motion, t->press_time) : t->press_time;
    if (elapsed(now, moved) >= still_ms) {
        rearm(t);
        return YG_REARM_STILL;
    }
    return YG_REARM_NONE;
}
