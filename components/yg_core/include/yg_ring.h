/* An append-only log over raw flash sectors, safe against power cuts. The oldest
 * sector is erased when the log wraps. See the design spec, section 9.3. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "yg_record.h"

#define YG_RING_SECTOR 4096u
#define YG_RING_HEADER 16u

typedef struct
{
    int (*read)(void *ctx, uint32_t off, void *buf, uint32_t len);
    int (*write)(void *ctx, uint32_t off, const void *buf, uint32_t len);
    int (*erase)(void *ctx, uint32_t off);   /* Erases one sector at off. */
    void *ctx;
    uint32_t size;   /* A whole number of sectors. */
} yg_ring_flash_t;

typedef struct
{
    yg_ring_flash_t f;
    uint32_t sectors;
    bool any;
    uint32_t head;
    uint32_t head_pos;
    uint32_t head_seq;
} yg_ring_t;

int yg_ring_open(yg_ring_t *r, const yg_ring_flash_t *f);
int yg_ring_append(yg_ring_t *r, const yg_record_t *rec);
int yg_ring_clear(yg_ring_t *r);

/* Finds the sector `age` steps back from the newest: 0 is the newest. Returns
 * false once past the oldest. first_trip is the trip the sector started in. */
bool yg_ring_sector_at(const yg_ring_t *r, uint32_t age, uint32_t *sector, uint32_t *first_trip);

/* The newest age that can hold the start of `trip`. A trip can span sectors, so
 * this looks for a sector that began before it, or else the oldest sector. */
uint32_t yg_ring_age_for_trip(const yg_ring_t *r, uint32_t trip);

/* Sector sequence numbers only grow, so a reader that walks them cannot skip or
 * repeat a sector when new records move the head on. */
bool yg_ring_oldest_seq(const yg_ring_t *r, uint32_t *seq);
bool yg_ring_newest_seq(const yg_ring_t *r, uint32_t *seq);
bool yg_ring_sector_for_seq(const yg_ring_t *r, uint32_t seq, uint32_t *sector, uint32_t *first_trip);

/* Clearing in steps, so that a caller can release its lock between sectors. Returns
 * 1 if it erased something, 0 if the sector held nothing, or -1 on failure. */
int yg_ring_clear_sector(yg_ring_t *r, uint32_t sector);
void yg_ring_reset(yg_ring_t *r);

/* Reads a whole sector into `buf`, which must hold YG_RING_SECTOR bytes. */
int yg_ring_read_sector(const yg_ring_t *r, uint32_t sector, uint8_t *buf);

/* Where records start inside a sector read by yg_ring_read_sector. */
#define YG_RING_FIRST_RECORD YG_RING_HEADER
