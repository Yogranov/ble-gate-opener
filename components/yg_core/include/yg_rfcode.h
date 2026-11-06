/* components/yg_core/include/yg_rfcode.h
 * A learned fixed-code OOK frame and the pure logic to learn and replay it. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define YG_RF_MAX_EDGES 300u   /* durations in one frame */
#define YG_RF_MAX_FRAMES 64u   /* frames examined in one capture */
#define YG_RF_GAP_MIN_US 3000u /* a carrier-off period this long separates frames */

typedef struct
{
    bool learned;
    uint16_t count;                  /* durations used in edges[] */
    uint16_t edges[YG_RF_MAX_EDGES]; /* microseconds, edges[0] is carrier-on */
    uint16_t gap_us;                 /* quiet gap between repeats of the frame */
} yg_rfcode_t;

/* True if every duration in a and b is within tolerance (25% or an 80 us floor). */
bool yg_rfcode_frames_match(const uint16_t *a, const uint16_t *b, uint16_t n);

/* Split a raw capture into frames at long carrier-off gaps, and return a frame of at
 * least `min_len` durations that repeats at least `min_repeats` times within tolerance.
 * Shorter frames are skipped, because noise spikes that repeat look like a tiny code.
 * first_high is true if durations[0] is carrier-on. Returns false if too few frames
 * agree, which is what a rolling or noisy code looks like. */
bool yg_rfcode_learn(const uint16_t *durations, uint16_t n, bool first_high,
                     uint8_t min_repeats, uint16_t min_len, yg_rfcode_t *out);

/* Write the on/off durations to transmit for `ms`, repeating frame+gap. Returns the
 * count written (<= max). out[0] is carrier-on. Zero if nothing is learned. */
uint16_t yg_rfcode_replay(const yg_rfcode_t *code, uint32_t ms, uint16_t *out, uint16_t max);
