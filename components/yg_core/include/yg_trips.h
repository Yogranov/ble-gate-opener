/* Builds a summary of one trip from its records. See the design spec, section 9. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "yg_record.h"

typedef struct
{
    uint32_t trip;
    bool seen;
    char fw[17];
    uint8_t reset;
    uint32_t duration_ms;
    bool heard;
    int8_t first_heard_rssi;
    uint32_t first_heard_ms;
    bool moved;
    uint8_t first_motion_cause;
    uint32_t first_motion_ms;
    uint16_t presses;
    uint8_t first_press_reason;
    int8_t first_press_rssi;
    uint32_t first_press_ms;
    int8_t parked_best;
    int8_t peak_rssi;
    uint32_t peak_ms;
    uint32_t adverts;
    bool has_temp;
    int16_t temp_x10;
    uint8_t errors;
    uint8_t rating;
} yg_trip_summary_t;

void yg_trip_summary_init(yg_trip_summary_t *s, uint32_t trip);

/* Feed records oldest first. A rating record counts when it rates this trip. */
void yg_trip_summary_add(yg_trip_summary_t *s, const yg_record_t *r);
int yg_trip_summary_to_json(const yg_trip_summary_t *s, char *buf, size_t len);
