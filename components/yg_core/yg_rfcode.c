#include "yg_rfcode.h"

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
