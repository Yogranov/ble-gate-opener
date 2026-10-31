#include "yg_trace.h"

#include <string.h>

#define HEARD_RECENT_MS 30000u
#define YOUNG_TRIP_MS   120000u
#define STILL_MS        120000u
#define FLUSH_MS        5000u

static void clear_period(yg_trace_t *t, uint32_t now) {
    t->period_start = now;
    t->best_rssi = 0;
    t->adverts = 0;
    t->accel_peak = 0;
    t->yaw_peak = 0;
    t->flags = 0;
}

/* Seconds per sample right now, or 0 for no samples. */
static uint8_t sample_period(const yg_trace_t *t, uint32_t now) {
    bool heard = t->has_advert && now - t->last_advert < HEARD_RECENT_MS;
    if (!heard && now - t->start >= YOUNG_TRIP_MS) {
        return 0;
    }
    uint32_t still_for = now - (t->has_motion ? t->last_motion : t->start);
    return still_for > STILL_MS ? 30 : 1;
}

void yg_trace_init(yg_trace_t *t, uint32_t now) {
    memset(t, 0, sizeof(*t));
    t->start = now;
    clear_period(t, now);
}

void yg_trace_on_advert(yg_trace_t *t, uint32_t now, int8_t rssi) {
    t->has_advert = true;
    t->last_advert = now;
    if (t->adverts == 0 || rssi > t->best_rssi) {
        t->best_rssi = rssi;
    }
    if (t->adverts < 255) {
        t->adverts++;
    }
}

void yg_trace_on_motion(yg_trace_t *t, uint32_t now) {
    t->has_motion = true;
    t->last_motion = now;
    t->flags |= YG_TRACE_FLAG_MOVED;
}

void yg_trace_on_peaks(yg_trace_t *t, uint16_t accel_mg, uint16_t yaw_dps) {
    if (accel_mg > t->accel_peak) {
        t->accel_peak = accel_mg;
    }
    if (yaw_dps > t->yaw_peak) {
        t->yaw_peak = yaw_dps;
    }
}

void yg_trace_on_flags(yg_trace_t *t, uint8_t flags) {
    t->flags |= flags;
}

bool yg_trace_tick(yg_trace_t *t, uint32_t now, uint32_t trip, yg_record_t *out) {
    uint8_t period = sample_period(t, now);
    if (period != t->cur_period) {
        /* What was gathered so far joins the first sample at the new rate. */
        t->cur_period = period;
        t->period_start = now;
    }
    if (t->rec_open && period != t->rec_period) {
        *out = t->rec;
        t->rec_open = false;
        return true;
    }
    if (period == 0) {
        clear_period(t, now);
        return false;
    }
    if (now - t->period_start >= (uint32_t)period * 1000u) {
        if (!t->rec_open) {
            yg_rec_trace_begin(&t->rec, trip, t->period_start, period);
            t->rec_open = true;
            t->rec_opened = t->period_start;
            t->rec_period = period;
        }
        yg_rec_trace_add(&t->rec, t->best_rssi, t->adverts, t->accel_peak, t->yaw_peak, t->flags);
        clear_period(t, t->period_start + (uint32_t)period * 1000u);
    }
    if (t->rec_open && (yg_rec_trace_count(&t->rec) >= YG_TRACE_MAX_SAMPLES || now - t->rec_opened >= FLUSH_MS)) {
        *out = t->rec;
        t->rec_open = false;
        return true;
    }
    return false;
}
