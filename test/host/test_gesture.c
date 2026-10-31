#include "yg_gesture.h"
#include "yg_test.h"

static yg_gesture_detector_t g;
static uint32_t now;
static int seen[5];

static void setup(void) {
    yg_gesture_init(&g);
    now = 1000;
    memset(seen, 0, sizeof(seen));
}

static void level(bool pressed, uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 20) {
        seen[yg_gesture_update(&g, now, pressed)]++;
        now += 20;
    }
}

static void click(void) {
    level(true, 100);
    level(false, 100);
}

static void single_click_fires_after_the_wait(void) {
    setup();
    level(true, 100);
    level(false, 300);
    CHECK_INT(seen[YG_GESTURE_SINGLE], 0);
    level(false, 200);
    CHECK_INT(seen[YG_GESTURE_SINGLE], 1);
    level(false, 2000);
    CHECK_INT(seen[YG_GESTURE_SINGLE], 1);
}

static void double_click(void) {
    setup();
    click();
    click();
    level(false, 500);
    CHECK_INT(seen[YG_GESTURE_DOUBLE], 1);
    CHECK_INT(seen[YG_GESTURE_SINGLE], 0);
}

static void triple_click(void) {
    setup();
    click();
    click();
    click();
    level(false, 500);
    CHECK_INT(seen[YG_GESTURE_TRIPLE], 1);
    CHECK_INT(seen[YG_GESTURE_DOUBLE], 0);
    CHECK_INT(seen[YG_GESTURE_SINGLE], 0);
}

static void hold_fires_once_and_release_adds_no_click(void) {
    setup();
    level(true, 2900);
    CHECK_INT(seen[YG_GESTURE_HOLD], 0);
    level(true, 200);
    CHECK_INT(seen[YG_GESTURE_HOLD], 1);
    level(true, 5000);
    CHECK_INT(seen[YG_GESTURE_HOLD], 1);
    level(false, 1000);
    CHECK_INT(seen[YG_GESTURE_SINGLE], 0);
}

static void bounce_is_ignored(void) {
    setup();
    /* 10 ms bounce shorter than the debounce time, sampled faster than usual. */
    for (int i = 0; i < 4; i++) {
        seen[yg_gesture_update(&g, now, true)]++;
        now += 10;
        seen[yg_gesture_update(&g, now, false)]++;
        now += 10;
    }
    level(false, 1000);
    CHECK_INT(seen[YG_GESTURE_SINGLE], 0);

    level(true, 100);
    for (int i = 0; i < 4; i++) {
        seen[yg_gesture_update(&g, now, false)]++;
        now += 10;
        seen[yg_gesture_update(&g, now, true)]++;
        now += 10;
    }
    level(true, 60);
    level(false, 1000);
    CHECK_INT(seen[YG_GESTURE_SINGLE], 1);
}

static void unfitted_button_does_nothing(void) {
    setup();
    level(false, 60000);
    for (int i = 1; i < 5; i++) CHECK_INT(seen[i], 0);
}

int main(void) {
    RUN(single_click_fires_after_the_wait);
    RUN(double_click);
    RUN(triple_click);
    RUN(hold_fires_once_and_release_adds_no_click);
    RUN(bounce_is_ignored);
    RUN(unfitted_button_does_nothing);
    return TEST_RESULT();
}
