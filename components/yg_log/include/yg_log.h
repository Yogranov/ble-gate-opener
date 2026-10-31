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

/* One JSON summary per trip, newest first, for trips below `before`. */
int yg_log_trip_list(uint32_t before, uint32_t limit, yg_log_line_cb_t cb, void *ctx);

int yg_log_clear(void);

/* How much of each log is in use, in sectors. */
void yg_log_usage(uint32_t *trip_used, uint32_t *trip_total, uint32_t *capture_used, uint32_t *capture_total);
