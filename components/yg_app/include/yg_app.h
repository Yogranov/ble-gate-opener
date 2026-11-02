/* The one decision task. All events reach the engine through its queue, so
 * decisions never race each other. See the design spec, section 10.2. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "yg_engine.h"

/* What the network side provides. Any of these may be NULL. */
typedef struct
{
    void (*hotspot)(bool on);
    bool (*hotspot_running)(void);
    void (*env_start)(void);
    void (*env_finish)(yg_env_t *out);
} yg_app_hooks_t;

void yg_app_start(const yg_app_hooks_t *hooks);

typedef void (*yg_app_fn_t)(yg_engine_t *e, uint32_t now, void *arg);

/* Runs fn in the decision task and waits for it to finish. Other tasks use this
 * to read or change anything owned by the engine, including settings. */
void yg_app_call(yg_app_fn_t fn, void *arg);

/* Safe from any task. */
void yg_app_post_clients(uint8_t clients);
uint32_t yg_app_current_trip(void);

/* For use inside yg_app_call, after a setting in e->s has changed. */
void yg_app_setting_changed(yg_engine_t *e, uint32_t now, const char *key, const char *value);

/* Erases the settings, beacon and logs, which can take a minute. Restart afterwards. */
void yg_app_erase_everything(void);

/* Puts every setting back to its default, keeping the beacon and logs. Call it from a function given to yg_app_call. */
void yg_app_settings_reset(yg_engine_t *e, uint32_t now);

/* Reset logs, from functions given to yg_app_call. Before the erase, this trip becomes number 1.
 * After an erase that worked, the stored counter follows, so a power cut in between only
 * leaves a higher trip number at the next start. */
void yg_app_trips_restart(yg_engine_t *e);
void yg_app_trips_restarted(yg_engine_t *e, uint32_t now);
