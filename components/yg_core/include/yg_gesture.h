/* Turns button level over time into gestures. See the design spec, section 8.4. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    YG_GESTURE_NONE,
    YG_GESTURE_SINGLE,
    YG_GESTURE_DOUBLE,
    YG_GESTURE_TRIPLE,
    YG_GESTURE_HOLD,
    YG_GESTURE_LEARN,
    YG_GESTURE_FACTORY_RESET,
} yg_gesture_t;

typedef struct
{
    bool stable;
    bool raw;
    uint32_t raw_since;
    uint32_t press_start;
    uint32_t last_release;
    uint8_t clicks;
    bool hold_fired;
    bool reset_fired;
    bool seen_released;
} yg_gesture_detector_t;

void yg_gesture_init(yg_gesture_detector_t *g);

/* Call regularly, for example every 20 ms, with the current button level. */
yg_gesture_t yg_gesture_update(yg_gesture_detector_t *g, uint32_t now, bool pressed);
