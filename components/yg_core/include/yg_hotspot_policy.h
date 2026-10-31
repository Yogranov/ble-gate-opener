/* Decides when the hotspot should be on. See the design spec, section 8.1. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "yg_settings.h"

typedef struct
{
    bool on;
    bool manual;        /* Started on request, so only the timer stops it. */
    bool motion_seen;
    bool had_client;
    uint8_t clients;
    uint32_t started;
    uint32_t last_client;
} yg_hotspot_policy_t;

/* force_on is for the first boot after a firmware update. */
void yg_hotspot_init(yg_hotspot_policy_t *h, const yg_settings_t *s, uint32_t now, bool force_on);
void yg_hotspot_on_motion(yg_hotspot_policy_t *h);
void yg_hotspot_set_clients(yg_hotspot_policy_t *h, uint32_t now, uint8_t clients);
void yg_hotspot_request(yg_hotspot_policy_t *h, uint32_t now);

/* Returns the state the hotspot should be in right now. */
bool yg_hotspot_tick(yg_hotspot_policy_t *h, const yg_settings_t *s, uint32_t now);

/* Seconds until the timer stops it, or 0 when off. */
uint32_t yg_hotspot_left_s(const yg_hotspot_policy_t *h, const yg_settings_t *s, uint32_t now);
