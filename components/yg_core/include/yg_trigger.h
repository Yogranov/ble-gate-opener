/* Decides when to press the remote. See the design spec, sections 5.2 and 5.3. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "yg_settings.h"

typedef enum
{
    YG_TRIG_UNPAIRED,
    YG_TRIG_ARMED,
    YG_TRIG_PRESSING,
    YG_TRIG_HOLD,
} yg_trig_state_t;

typedef enum
{
    YG_PRESS_NONE,
    YG_PRESS_AUTO,
    YG_PRESS_BUTTON,
    YG_PRESS_WEB,
    YG_PRESS_FALLBACK,
} yg_press_reason_t;

typedef enum
{
    YG_REARM_NONE,
    YG_REARM_GONE,
    YG_REARM_STILL,
    YG_REARM_MOVED,
} yg_rearm_reason_t;

typedef struct
{
    yg_trig_state_t state;
    bool paired;
    bool suspended;
    bool sensor_ok;
    bool has_motion;
    bool has_advert;
    bool motion_unused;    /* Motion since the last press or re-arm. */
    bool until_gone;       /* The last press came from the fixed threshold. */
    uint32_t last_motion;
    uint32_t last_advert;
    uint32_t press_time;
} yg_trigger_t;

void yg_trigger_init(yg_trigger_t *t, bool paired);
void yg_trigger_set_paired(yg_trigger_t *t, bool paired);
void yg_trigger_set_suspended(yg_trigger_t *t, bool suspended);
void yg_trigger_set_sensor_ok(yg_trigger_t *t, bool ok);
void yg_trigger_on_motion(yg_trigger_t *t, uint32_t now);

/* Returns a press reason when the caller must start a press now. */
yg_press_reason_t yg_trigger_on_advert(yg_trigger_t *t, const yg_settings_t *s, uint32_t now, int8_t rssi);
yg_press_reason_t yg_trigger_manual(yg_trigger_t *t, uint32_t now, yg_press_reason_t reason);

/* Advances press and hold timers. Returns why it re-armed, if it just did. */
yg_rearm_reason_t yg_trigger_tick(yg_trigger_t *t, const yg_settings_t *s, uint32_t now);

/* True while the motion check is replaced by the fixed threshold rule. */
bool yg_trigger_fallback_active(const yg_trigger_t *t, const yg_settings_t *s);

/* The set point in force right now, or YG_SET_POINT_ANY. */
int8_t yg_trigger_effective_set_point(const yg_trigger_t *t, const yg_settings_t *s);

bool yg_trigger_moved_recently(const yg_trigger_t *t, const yg_settings_t *s, uint32_t now);
