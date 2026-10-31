#include "yg_trips.h"

#include <string.h>

#include "yg_json.h"
#include "yg_motion.h"

void yg_trip_summary_init(yg_trip_summary_t *s, uint32_t trip) {
    memset(s, 0, sizeof(*s));
    s->trip = trip;
}

static void note_time(yg_trip_summary_t *s, uint32_t t) {
    if (t > s->duration_ms) {
        s->duration_ms = t;
    }
}

static void add_trace(yg_trip_summary_t *s, const yg_record_t *r) {
    uint8_t period = r->payload[0];
    uint8_t count = r->payload[1];
    for (uint8_t i = 0; i < count; i++) {
        const uint8_t *sample = r->payload + 2 + i * YG_TRACE_SAMPLE_SIZE;
        int8_t rssi = (int8_t)sample[0];
        uint32_t t = r->uptime_ms + (uint32_t)i * period * 1000u;
        s->adverts += sample[1];
        note_time(s, t);
        if (sample[1] == 0) {
            continue;
        }
        if (s->peak_rssi == 0 || rssi > s->peak_rssi) {
            s->peak_rssi = rssi;
            s->peak_ms = t;
        }
        if ((!s->moved || t < s->first_motion_ms) && (s->parked_best == 0 || rssi > s->parked_best)) {
            s->parked_best = rssi;
        }
    }
}

void yg_trip_summary_add(yg_trip_summary_t *s, const yg_record_t *r) {
    const uint8_t *p = r->payload;
    if (r->type == YG_REC_RATING && yg_get_u32(p) == s->trip) {
        s->rating = p[4];
    }
    if (r->trip != s->trip) {
        return;
    }
    s->seen = true;
    note_time(s, r->uptime_ms);
    switch (r->type) {
    case YG_REC_BOOT:
        memcpy(s->fw, p, 16);
        s->fw[16] = 0;
        s->reset = p[16];
        break;
    case YG_REC_FIRST_HEARD:
        if (!s->heard) {
            s->heard = true;
            s->first_heard_rssi = (int8_t)p[0];
            s->first_heard_ms = r->uptime_ms;
        }
        break;
    case YG_REC_MOTION:
        if (!s->moved) {
            s->moved = true;
            s->first_motion_cause = p[0];
            s->first_motion_ms = r->uptime_ms;
        }
        break;
    case YG_REC_PRESS:
        if (s->presses == 0) {
            s->first_press_reason = p[0];
            s->first_press_rssi = (int8_t)p[1];
            s->first_press_ms = r->uptime_ms;
        }
        s->presses++;
        break;
    case YG_REC_TEMP:
        if (!s->has_temp) {
            s->has_temp = true;
            s->temp_x10 = (int16_t)yg_get_u16(p);
        }
        break;
    case YG_REC_ERROR:
        if (s->errors < 255) {
            s->errors++;
        }
        break;
    case YG_REC_TRACE:
        add_trace(s, r);
        break;
    default:
        break;
    }
}

int yg_trip_summary_to_json(const yg_trip_summary_t *s, char *buf, size_t len) {
    yg_jw_t w;
    yg_jw_init(&w, buf, len);
    yg_jw_raw(&w, "{\"trip\":%u,\"fw\":", (unsigned)s->trip);
    yg_jw_string(&w, s->fw);
    yg_jw_raw(&w, ",\"reset\":%u,\"duration_ms\":%u,\"first_heard\":", s->reset, (unsigned)s->duration_ms);
    if (s->heard) {
        yg_jw_raw(&w, "{\"t\":%u,\"rssi\":%d}", (unsigned)s->first_heard_ms, (int)s->first_heard_rssi);
    } else {
        yg_jw_raw(&w, "null");
    }
    yg_jw_raw(&w, ",\"first_motion\":");
    if (s->moved) {
        yg_jw_raw(&w, "{\"t\":%u,\"cause\":\"%s\"}", (unsigned)s->first_motion_ms,
                  s->first_motion_cause == YG_MOTION_YAW ? "yaw" : "accel");
    } else {
        yg_jw_raw(&w, "null");
    }
    yg_jw_raw(&w, ",\"presses\":%u,\"first_press\":", s->presses);
    if (s->presses) {
        yg_jw_raw(&w, "{\"t\":%u,\"reason\":\"%s\",\"rssi\":", (unsigned)s->first_press_ms, yg_press_reason_name(s->first_press_reason));
        if (s->first_press_rssi) {
            yg_jw_raw(&w, "%d}", (int)s->first_press_rssi);
        } else {
            yg_jw_raw(&w, "null}");
        }
    } else {
        yg_jw_raw(&w, "null");
    }
    yg_jw_raw(&w, ",\"parked_best\":");
    if (s->parked_best) {
        yg_jw_raw(&w, "%d", (int)s->parked_best);
    } else {
        yg_jw_raw(&w, "null");
    }
    yg_jw_raw(&w, ",\"peak\":");
    if (s->peak_rssi) {
        yg_jw_raw(&w, "{\"t\":%u,\"rssi\":%d}", (unsigned)s->peak_ms, (int)s->peak_rssi);
    } else {
        yg_jw_raw(&w, "null");
    }
    yg_jw_raw(&w, ",\"warning_ms\":");
    if (s->presses && s->peak_rssi && s->peak_ms >= s->first_press_ms) {
        yg_jw_raw(&w, "%u", (unsigned)(s->peak_ms - s->first_press_ms));
    } else {
        yg_jw_raw(&w, "null");
    }
    yg_jw_raw(&w, ",\"adverts\":%u,\"temp_c\":", (unsigned)s->adverts);
    if (s->has_temp) {
        yg_jw_tenths(&w, s->temp_x10);
    } else {
        yg_jw_raw(&w, "null");
    }
    yg_jw_raw(&w, ",\"errors\":%u,\"rating\":", s->errors);
    if (s->rating) {
        yg_jw_raw(&w, "\"%s\"", yg_rating_name(s->rating));
    } else {
        yg_jw_raw(&w, "null");
    }
    yg_jw_raw(&w, "}");
    return yg_jw_finish(&w);
}
