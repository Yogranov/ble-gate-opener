#include "yg_sound.h"
#include "yg_test.h"

#define TICK_MS 20
#define LONGEST_MS 3000

static void every_sound_ends_with_the_buzzer_off_and_stays_short(void) {
    for (int i = 0; i < YG_BUZZ_COUNT; i++) {
        const yg_sound_t *sound = yg_sound_get((yg_buzz_event_t)i);
        int total = 0;
        CHECK(sound->count % 2 == 1);
        for (int step = 0; step < sound->count; step++) {
            CHECK(sound->steps[step] >= TICK_MS);
            CHECK(sound->steps[step] % TICK_MS == 0);
            total += sound->steps[step];
        }
        CHECK(total <= LONGEST_MS);
    }
}

static void every_sound_has_its_own_rhythm(void) {
    for (int a = 0; a < YG_BUZZ_COUNT; a++) {
        for (int b = a + 1; b < YG_BUZZ_COUNT; b++) {
            const yg_sound_t *x = yg_sound_get((yg_buzz_event_t)a), *y = yg_sound_get((yg_buzz_event_t)b);
            CHECK(x->count != y->count || memcmp(x->steps, y->steps, x->count * sizeof(x->steps[0])) != 0);
        }
    }
}

static void sounds_are_found_by_name(void) {
    yg_buzz_event_t found;
    for (int i = 0; i < YG_BUZZ_COUNT; i++) {
        CHECK(yg_sound_parse(yg_sound_get((yg_buzz_event_t)i)->name, &found));
        CHECK_INT(found, i);
    }
    CHECK(yg_sound_parse("success", &found));
    CHECK_INT(found, YG_BUZZ_SAVED);
    CHECK(yg_sound_parse("error", &found));
    CHECK_INT(found, YG_BUZZ_REJECTED);
    CHECK(!yg_sound_parse("loud", &found));
}

int main(void) {
    RUN(every_sound_ends_with_the_buzzer_off_and_stays_short);
    RUN(every_sound_has_its_own_rhythm);
    RUN(sounds_are_found_by_name);
    return TEST_RESULT();
}
