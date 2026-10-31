#include "yg_gesture.h"

#include <string.h>

#define DEBOUNCE_MS  30u
#define CLICK_GAP_MS 400u
#define HOLD_MS      3000u

void yg_gesture_init(yg_gesture_detector_t *g) {
    memset(g, 0, sizeof(*g));
}

yg_gesture_t yg_gesture_update(yg_gesture_detector_t *g, uint32_t now, bool pressed) {
    if (pressed != g->raw) {
        g->raw = pressed;
        g->raw_since = now;
    }
    if (g->raw != g->stable && now - g->raw_since >= DEBOUNCE_MS) {
        g->stable = g->raw;
        if (g->stable) {
            g->press_start = now;
            g->hold_fired = false;
        } else if (!g->hold_fired) {
            g->clicks++;
            g->last_release = now;
        }
    }
    if (g->stable) {
        if (!g->hold_fired && now - g->press_start >= HOLD_MS) {
            g->hold_fired = true;
            g->clicks = 0;
            return YG_GESTURE_HOLD;
        }
        return YG_GESTURE_NONE;
    }
    if (g->clicks > 0 && now - g->last_release >= CLICK_GAP_MS) {
        uint8_t clicks = g->clicks;
        g->clicks = 0;
        if (clicks == 1) return YG_GESTURE_SINGLE;
        if (clicks == 2) return YG_GESTURE_DOUBLE;
        return YG_GESTURE_TRIPLE;
    }
    return YG_GESTURE_NONE;
}
