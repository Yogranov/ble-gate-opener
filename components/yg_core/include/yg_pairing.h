/* Finds the beacon to pair with. See the design spec, section 7. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "yg_ibeacon.h"
#include "yg_settings.h"
#include "yg_types.h"

#define YG_PAIR_MAX_CANDIDATES 16

typedef enum
{
    YG_PAIR_IDLE,
    YG_PAIR_AUTO,   /* Saves a beacon held close. */
    YG_PAIR_LIST,   /* Only lists beacons for the page to choose from. */
} yg_pair_mode_t;

typedef enum
{
    YG_PAIR_RESULT_NONE,
    YG_PAIR_RESULT_SAVED,
    YG_PAIR_RESULT_TIMEOUT,
} yg_pair_result_t;

typedef struct
{
    bool used;
    yg_addr_t addr;
    float level;
    uint32_t last_seen;
    uint32_t count;
    yg_ibeacon_t ib;
} yg_pair_candidate_t;

typedef struct
{
    yg_pair_mode_t mode;
    uint32_t started;
    yg_pair_candidate_t c[YG_PAIR_MAX_CANDIDATES];
    int8_t leader;
    uint32_t leader_since;
} yg_pairing_t;

void yg_pairing_init(yg_pairing_t *p);
void yg_pairing_start(yg_pairing_t *p, yg_pair_mode_t mode, uint32_t now);
void yg_pairing_cancel(yg_pairing_t *p);
void yg_pairing_on_advert(yg_pairing_t *p, uint32_t now, const yg_addr_t *a, int8_t rssi, const yg_ibeacon_t *ib);
yg_pair_result_t yg_pairing_tick(yg_pairing_t *p, const yg_settings_t *s, uint32_t now, yg_addr_t *winner);
int yg_pairing_candidates_json(const yg_pairing_t *p, const yg_settings_t *s, uint32_t now, char *buf, size_t len);
