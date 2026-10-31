/* The trip log and debug captures, on raw flash partitions. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "yg_record.h"

void yg_log_init(void);
void yg_log_trip(const yg_record_t *r);
void yg_log_capture(const yg_record_t *r);

/* Called once per JSON line, without a newline. Return false to stop early. */
typedef bool (*yg_log_line_cb_t)(const char *line, size_t len, void *ctx);

/* All records, oldest first. Captures come after the trip log if asked for. */
int yg_log_export(bool captures, yg_log_line_cb_t cb, void *ctx);

/* The records of one trip. */
int yg_log_trip_records(uint32_t trip, yg_log_line_cb_t cb, void *ctx);

/* One JSON summary per trip, newest first, for up to `limit` trip numbers below
 * `before`. Not every number has a trip, as a power cut can leave gaps. next_before
 * is where to go on from, or 0 if nothing older is stored. */
int yg_log_trip_list(uint32_t before, uint32_t limit, uint32_t *next_before, yg_log_line_cb_t cb, void *ctx);

/* Records logged while this runs are dropped. It can take most of a minute on full
 * logs, but holds the lock for one sector at a time, so presses are not held up. */
int yg_log_clear(void);

/* How much of each log is in use, in sectors. */
void yg_log_usage(uint32_t *trip_used, uint32_t *trip_total, uint32_t *capture_used, uint32_t *capture_total);
