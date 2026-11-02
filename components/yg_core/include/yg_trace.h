/* Collects per-second samples into trace records. See the design spec, section 9.2. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "yg_record.h"

#define YG_TRACE_FLAG_MOVED     0x01
#define YG_TRACE_FLAG_NEAR_MISS 0x02
#define YG_TRACE_FLAG_TILTING   0x04

typedef struct
{
    uint32_t start;
    bool has_advert;
    uint32_t last_advert;
    bool has_motion;
    uint32_t last_motion;

    uint8_t cur_period;
    uint32_t period_start;
    int8_t best_rssi;
    uint8_t adverts;
    uint16_t accel_peak;
    uint16_t yaw_peak;
    uint8_t flags;

    yg_record_t rec;
    bool rec_open;
    uint32_t rec_opened;
    uint8_t rec_period;
} yg_trace_t;

void yg_trace_init(yg_trace_t *t, uint32_t now);
void yg_trace_on_advert(yg_trace_t *t, uint32_t now, int8_t rssi);
void yg_trace_on_motion(yg_trace_t *t, uint32_t now);
void yg_trace_on_peaks(yg_trace_t *t, uint16_t accel_mg, uint16_t yaw_dps);
void yg_trace_on_flags(yg_trace_t *t, uint8_t flags);

/* A trace already being collected keeps its samples, under this trip number. */
void yg_trace_set_trip(yg_trace_t *t, uint32_t trip);

/* True in the first 2 minutes of a trip, and while the beacon has been heard in the last 30 s. */
bool yg_trace_is_sampling(const yg_trace_t *t, uint32_t now);

/* Call about every 100 ms. Returns true when `out` holds a record to log. */
bool yg_trace_tick(yg_trace_t *t, uint32_t now, uint32_t trip, yg_record_t *out);
