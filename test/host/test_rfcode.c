#include "yg_rfcode.h"
#include "yg_test.h"

#include <string.h>

static void frames_match_within_tolerance(void) {
    uint16_t a[] = {300, 900, 300, 900, 300};
    uint16_t b[] = {320, 870, 290, 930, 310}; /* each within ~25% or the 80us floor */
    uint16_t c[] = {300, 900, 300, 1500, 300}; /* one edge far off */
    CHECK(yg_rfcode_frames_match(a, b, 5));
    CHECK(!yg_rfcode_frames_match(a, c, 5));
}

/* Builds a capture: `reps` copies of `frame` (odd `len`, carrier-on at each end),
 * each followed by a long carrier-off gap. The toy frames here are shorter than any
 * remote's, so most tests learn with no floor on the frame length (min_len 1). */
static uint16_t build(uint16_t *buf, const uint16_t *frame, uint16_t len, uint16_t reps, uint16_t gap) {
    uint16_t w = 0;
    for (uint16_t r = 0; r < reps; r++) {
        memcpy(&buf[w], frame, len * sizeof(uint16_t));
        w += len;
        buf[w++] = gap;
    }
    return w;
}

static void learns_a_repeating_frame(void) {
    uint16_t frame[] = {300, 900, 300, 900, 300};
    uint16_t buf[256];
    uint16_t n = build(buf, frame, 5, 5, 10000);
    yg_rfcode_t out = {0};
    CHECK(yg_rfcode_learn(buf, n, true, 3, 1, &out));
    CHECK(out.learned);
    CHECK_INT(out.count, 5);
    CHECK(yg_rfcode_frames_match(out.edges, frame, 5));
    CHECK(out.gap_us >= 9000 && out.gap_us <= 11000);
}

static void rejects_too_few_repeats(void) {
    uint16_t frame[] = {300, 900, 300};
    uint16_t buf[64];
    uint16_t n = build(buf, frame, 3, 2, 10000);
    yg_rfcode_t out = {0};
    CHECK(!yg_rfcode_learn(buf, n, true, 3, 1, &out));
}

/* Spikes that repeat look like a one-edge code. With a floor on the frame length they
 * are skipped, and the real frame behind them is found. */
static void skips_frames_too_short_to_be_a_remote(void) {
    uint16_t spike[] = {50};
    uint16_t frame[25];
    for (uint16_t i = 0; i < 24; i += 2) {
        frame[i] = (i % 4 == 0) ? 900 : 300;
        frame[i + 1] = (i % 4 == 0) ? 300 : 900;
    }
    frame[24] = 300;
    uint16_t buf[256];
    uint16_t n = build(buf, spike, 1, 4, 5000);
    n += build(&buf[n], frame, 25, 3, 10000);
    yg_rfcode_t out = {0};
    CHECK(yg_rfcode_learn(buf, n, true, 3, 1, &out));
    CHECK_INT(out.count, 1); /* with no floor, the spikes win */
    memset(&out, 0, sizeof(out));
    CHECK(yg_rfcode_learn(buf, n, true, 3, 16, &out));
    CHECK_INT(out.count, 25);
    CHECK(yg_rfcode_frames_match(out.edges, frame, 25));
}

static void rejects_rolling_code(void) {
    /* Five same-length frames that differ at carrier-off positions: replay could
     * never open the gate, so learning must refuse. */
    uint16_t frames[5][5] = {
        {300, 300, 300, 300, 300},
        {300, 1200, 300, 300, 300},
        {300, 300, 300, 1200, 300},
        {300, 1200, 300, 1200, 300},
        {300, 2400, 300, 300, 300},
    };
    uint16_t buf[64];
    uint16_t w = 0;
    for (uint16_t r = 0; r < 5; r++) {
        memcpy(&buf[w], frames[r], 5 * sizeof(uint16_t));
        w += 5;
        buf[w++] = 10000;
    }
    yg_rfcode_t out = {0};
    CHECK(!yg_rfcode_learn(buf, w, true, 3, 1, &out));
}

static void tolerates_jitter(void) {
    uint16_t f1[] = {300, 900, 300, 900, 300};
    uint16_t f2[] = {320, 880, 290, 915, 310};
    uint16_t f3[] = {290, 910, 310, 890, 305};
    uint16_t buf[64];
    uint16_t w = 0;
    memcpy(&buf[w], f1, sizeof(f1)); w += 5; buf[w++] = 10000;
    memcpy(&buf[w], f2, sizeof(f2)); w += 5; buf[w++] = 10000;
    memcpy(&buf[w], f3, sizeof(f3)); w += 5; buf[w++] = 10000;
    yg_rfcode_t out = {0};
    CHECK(yg_rfcode_learn(buf, w, true, 3, 1, &out));
    CHECK_INT(out.count, 5);
}

static void replays_for_the_press_duration(void) {
    yg_rfcode_t code = {.learned = true, .count = 4, .gap_us = 10000};
    uint16_t f[] = {300, 900, 300, 900};
    memcpy(code.edges, f, sizeof(f));
    uint16_t out[512];
    /* frame = 2400us, cycle = 12400us; a 300ms press fits about 24 cycles. */
    uint16_t w = yg_rfcode_replay(&code, 300, out, 512);
    CHECK(w > 0);
    CHECK_INT(out[0], 300);
    CHECK(w >= 4 * 20);
    uint16_t cap = yg_rfcode_replay(&code, 300, out, 10); /* respect max */
    CHECK(cap <= 10);
    yg_rfcode_t none = {0};
    CHECK_INT(yg_rfcode_replay(&none, 300, out, 512), 0);
}

int main(void) {
    frames_match_within_tolerance();
    learns_a_repeating_frame();
    rejects_too_few_repeats();
    skips_frames_too_short_to_be_a_remote();
    rejects_rolling_code();
    tolerates_jitter();
    replays_for_the_press_duration();
    printf("%d checks, %d failures\n", yg_test_checks, yg_test_failures);
    return yg_test_failures != 0;
}
