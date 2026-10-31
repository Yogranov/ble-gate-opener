/* A small append-only JSON text writer, safe against overflow. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct
{
    char *buf;
    size_t len;
    size_t pos;
    bool overflow;
} yg_jw_t;

void yg_jw_init(yg_jw_t *w, char *buf, size_t len);
void yg_jw_raw(yg_jw_t *w, const char *fmt, ...);
void yg_jw_string(yg_jw_t *w, const char *text);   /* Writes "text" with escaping. */
void yg_jw_tenths(yg_jw_t *w, int tenths);         /* Writes -185 as -18.5. */
int yg_jw_finish(yg_jw_t *w);                      /* Returns the length, or -1 on overflow. */
