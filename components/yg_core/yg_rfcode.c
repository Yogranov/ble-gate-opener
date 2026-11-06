#include "yg_rfcode.h"

#include <string.h>

#define TOL_PERCENT 25u
#define TOL_FLOOR_US 80u

static bool close_enough(uint16_t a, uint16_t b) {
    uint16_t hi = a > b ? a : b;
    uint16_t lo = a > b ? b : a;
    uint32_t slack = (uint32_t)a * TOL_PERCENT / 100u;
    if (slack < TOL_FLOOR_US) slack = TOL_FLOOR_US;
    return (uint32_t)(hi - lo) <= slack;
}

bool yg_rfcode_frames_match(const uint16_t *a, const uint16_t *b, uint16_t n) {
    for (uint16_t i = 0; i < n; i++) {
        if (!close_enough(a[i], b[i])) return false;
    }
    return true;
}

bool yg_rfcode_learn(const uint16_t *durations, uint16_t n, bool first_high,
                     uint8_t min_repeats, yg_rfcode_t *out) {
    uint16_t start[YG_RF_MAX_FRAMES], len[YG_RF_MAX_FRAMES], gap[YG_RF_MAX_FRAMES];
    uint16_t frames = 0, span = 0;
    bool high = first_high;
    for (uint16_t i = 0; i < n; i++) {
        if (!high && durations[i] >= YG_RF_GAP_MIN_US) { /* a gap ends a frame */
            if (i > span && frames < YG_RF_MAX_FRAMES) {
                start[frames] = span;
                len[frames] = (uint16_t)(i - span);
                gap[frames] = durations[i];
                frames++;
            }
            span = (uint16_t)(i + 1); /* the next frame starts carrier-on */
        }
        high = !high;
    }
    if (frames < min_repeats) return false;
    for (uint16_t f = 0; f + min_repeats <= frames; f++) {
        if (len[f] == 0 || len[f] > YG_RF_MAX_EDGES) continue;
        uint16_t agree = 1;
        for (uint16_t g = f + 1; g < frames; g++) {
            if (len[g] == len[f] &&
                yg_rfcode_frames_match(&durations[start[f]], &durations[start[g]], len[f])) {
                agree++;
            }
        }
        if (agree >= min_repeats) {
            out->learned = true;
            out->count = len[f];
            memcpy(out->edges, &durations[start[f]], (size_t)len[f] * sizeof(uint16_t));
            out->gap_us = gap[f];
            return true;
        }
    }
    return false;
}

uint16_t yg_rfcode_replay(const yg_rfcode_t *code, uint32_t ms, uint16_t *out, uint16_t max) {
    if (!code->learned || code->count == 0) return 0;
    uint32_t frame_us = 0;
    for (uint16_t i = 0; i < code->count; i++) frame_us += code->edges[i];
    uint32_t cycle_us = frame_us + code->gap_us;
    uint32_t budget_us = ms * 1000u;
    uint32_t elapsed = 0;
    uint16_t w = 0;
    while (w + code->count <= max) {
        for (uint16_t i = 0; i < code->count; i++) out[w++] = code->edges[i];
        elapsed += cycle_us;
        if (elapsed >= budget_us) break;
        if (w < max) out[w++] = code->gap_us;
    }
    return w;
}
