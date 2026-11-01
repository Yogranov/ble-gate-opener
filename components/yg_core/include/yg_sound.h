/* The buzzer's sounds. An active buzzer has one fixed tone, so a sound is a rhythm. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    YG_BUZZ_PRESS,       /* One short: a person pressed the remote. */
    YG_BUZZ_PAIRING,     /* Two short: listening, so hold the beacon against the unit. */
    YG_BUZZ_HOTSPOT,     /* Three short: the hotspot is starting. */
    YG_BUZZ_SAVED,       /* Short, short, long: success. */
    YG_BUZZ_REJECTED,    /* Two rattles: that did not work. */
    YG_BUZZ_NO_BEACON,   /* One long, at power-on: no beacon is paired yet. */
    YG_BUZZ_SENSOR,      /* Three long: the motion sensor failed. */
    YG_BUZZ_COUNT,
} yg_buzz_event_t;

typedef struct
{
    const char *name;        /* What the test endpoint and the page call it. */
    const uint16_t *steps;   /* Milliseconds, starting with the buzzer on and then taking turns. */
    uint8_t count;           /* Always odd, so a sound ends on a step with the buzzer on. */
} yg_sound_t;

const yg_sound_t *yg_sound_get(yg_buzz_event_t e);
bool yg_sound_parse(const char *name, yg_buzz_event_t *out);
