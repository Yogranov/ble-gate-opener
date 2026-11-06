/* components/yg_core/include/yg_rfcode.h
 * A learned fixed-code OOK frame and the pure logic to learn and replay it. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define YG_RF_MAX_EDGES 300u /* durations in one frame */

typedef struct
{
    bool learned;
    uint16_t count;                  /* durations used in edges[] */
    uint16_t edges[YG_RF_MAX_EDGES]; /* microseconds, edges[0] is carrier-on */
    uint16_t gap_us;                 /* quiet gap between repeats of the frame */
} yg_rfcode_t;

/* True if every duration in a and b is within tolerance (25% or an 80 us floor). */
bool yg_rfcode_frames_match(const uint16_t *a, const uint16_t *b, uint16_t n);
