#include "yg_sound.h"

#include <string.h>

#define COUNT_OF(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

/* The buzzer task runs every 20 ms, so every step is a multiple of that. */
static const uint16_t PRESS[] = { 80 };
static const uint16_t PAIRING[] = { 80, 120, 80 };
static const uint16_t HOTSPOT[] = { 80, 120, 80, 120, 80 };
static const uint16_t SAVED[] = { 80, 80, 80, 80, 320 };
static const uint16_t REJECTED[] = {
    40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40,
    160,
    40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40,
};
static const uint16_t NO_BEACON[] = { 700 };
static const uint16_t SENSOR[] = { 500, 200, 500, 200, 500 };
static const uint16_t ERASE[] = { 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 1000 };

static const yg_sound_t SOUNDS[YG_BUZZ_COUNT] = {
    [YG_BUZZ_PRESS] = { "press", PRESS, COUNT_OF(PRESS) },
    [YG_BUZZ_PAIRING] = { "pairing", PAIRING, COUNT_OF(PAIRING) },
    [YG_BUZZ_HOTSPOT] = { "hotspot", HOTSPOT, COUNT_OF(HOTSPOT) },
    [YG_BUZZ_SAVED] = { "success", SAVED, COUNT_OF(SAVED) },
    [YG_BUZZ_REJECTED] = { "error", REJECTED, COUNT_OF(REJECTED) },
    [YG_BUZZ_NO_BEACON] = { "no_beacon", NO_BEACON, COUNT_OF(NO_BEACON) },
    [YG_BUZZ_SENSOR] = { "sensor", SENSOR, COUNT_OF(SENSOR) },
    [YG_BUZZ_ERASE] = { "erase", ERASE, COUNT_OF(ERASE) },
};

const yg_sound_t *yg_sound_get(yg_buzz_event_t e) {
    return &SOUNDS[e];
}

bool yg_sound_parse(const char *name, yg_buzz_event_t *out) {
    for (int i = 0; i < YG_BUZZ_COUNT; i++) {
        if (strcmp(SOUNDS[i].name, name) == 0) {
            *out = (yg_buzz_event_t)i;
            return true;
        }
    }
    return false;
}
