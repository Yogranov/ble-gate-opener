#include "yg_rfcode.h"
#include "yg_test.h"

static void frames_match_within_tolerance(void) {
    uint16_t a[] = {300, 900, 300, 900, 300};
    uint16_t b[] = {320, 870, 290, 930, 310};   /* each within ~25% or the 80us floor */
    uint16_t c[] = {300, 900, 300, 1500, 300};  /* one edge far off */
    CHECK(yg_rfcode_frames_match(a, b, 5));
    CHECK(!yg_rfcode_frames_match(a, c, 5));
}

int main(void) {
    frames_match_within_tolerance();
    printf("%d checks, %d failures\n", yg_test_checks, yg_test_failures);
    return yg_test_failures != 0;
}
