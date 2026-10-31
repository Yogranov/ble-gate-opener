#include "yg_ring.h"

#include <string.h>

#define SECTOR_MAGIC 0x31524759u   /* "YGR1" */
#define CHUNK        128u

static uint32_t sector_offset(uint32_t sector) {
    return sector * YG_RING_SECTOR;
}

static bool read_header(const yg_ring_t *r, uint32_t sector, uint32_t *seq, uint32_t *first_trip) {
    uint8_t h[YG_RING_HEADER];
    if (r->f.read(r->f.ctx, sector_offset(sector), h, sizeof(h)) != 0) {
        return false;
    }
    if (yg_get_u32(h) != SECTOR_MAGIC || yg_get_u16(h + 12) != yg_crc16(h, 12)) {
        return false;
    }
    *seq = yg_get_u32(h + 4);
    *first_trip = yg_get_u32(h + 8);
    return true;
}

static int write_header(yg_ring_t *r, uint32_t sector, uint32_t seq, uint32_t first_trip) {
    uint8_t h[YG_RING_HEADER];
    memset(h, 0xFF, sizeof(h));
    const uint32_t words[3] = { SECTOR_MAGIC, seq, first_trip };
    for (int i = 0; i < 3; i++) {
        h[i * 4] = (uint8_t)words[i];
        h[i * 4 + 1] = (uint8_t)(words[i] >> 8);
        h[i * 4 + 2] = (uint8_t)(words[i] >> 16);
        h[i * 4 + 3] = (uint8_t)(words[i] >> 24);
    }
    uint16_t crc = yg_crc16(h, 12);
    h[12] = (uint8_t)crc;
    h[13] = (uint8_t)(crc >> 8);
    return r->f.write(r->f.ctx, sector_offset(sector), h, sizeof(h));
}

/* The next free 4-byte position: one past the last byte that is not 0xFF. */
static uint32_t find_write_pos(const yg_ring_t *r, uint32_t sector) {
    uint8_t chunk[CHUNK];
    for (uint32_t end = YG_RING_SECTOR; end > 0; end -= CHUNK) {
        if (r->f.read(r->f.ctx, sector_offset(sector) + end - CHUNK, chunk, CHUNK) != 0) {
            return YG_RING_SECTOR;
        }
        for (uint32_t i = CHUNK; i-- > 0;) {
            if (chunk[i] != 0xFF) {
                return (end - CHUNK + i + 1 + 3) & ~3u;
            }
        }
    }
    return YG_RING_HEADER;
}

static bool sector_is_blank(const yg_ring_t *r, uint32_t sector) {
    uint8_t chunk[CHUNK];
    for (uint32_t off = 0; off < YG_RING_SECTOR; off += CHUNK) {
        if (r->f.read(r->f.ctx, sector_offset(sector) + off, chunk, CHUNK) != 0) {
            return false;
        }
        for (uint32_t i = 0; i < CHUNK; i++) {
            if (chunk[i] != 0xFF) {
                return false;
            }
        }
    }
    return true;
}

int yg_ring_open(yg_ring_t *r, const yg_ring_flash_t *f) {
    memset(r, 0, sizeof(*r));
    r->f = *f;
    r->sectors = f->size / YG_RING_SECTOR;
    for (uint32_t s = 0; s < r->sectors; s++) {
        uint32_t seq, trip;
        if (read_header(r, s, &seq, &trip) && (!r->any || (int32_t)(seq - r->head_seq) > 0)) {
            r->any = true;
            r->head = s;
            r->head_seq = seq;
        }
    }
    if (r->any) {
        r->head_pos = find_write_pos(r, r->head);
    }
    return 0;
}

static int advance(yg_ring_t *r, uint32_t trip) {
    uint32_t next = r->any ? (r->head + 1) % r->sectors : 0;
    if (!sector_is_blank(r, next) && r->f.erase(r->f.ctx, sector_offset(next)) != 0) {
        return -1;
    }
    uint32_t seq = r->any ? r->head_seq + 1 : 1;
    if (write_header(r, next, seq, trip) != 0) {
        return -1;
    }
    r->any = true;
    r->head = next;
    r->head_seq = seq;
    r->head_pos = YG_RING_HEADER;
    return 0;
}

int yg_ring_append(yg_ring_t *r, const yg_record_t *rec) {
    uint8_t encoded[YG_RECORD_MAX_ENCODED];
    size_t n = yg_record_encode(rec, encoded);
    if (!r->any || r->head_pos + n > YG_RING_SECTOR) {
        int err = advance(r, rec->trip);
        if (err != 0) {
            return err;
        }
    }
    uint32_t off = sector_offset(r->head) + r->head_pos;
    /* Advance even on failure: the torn bytes cannot be written over. */
    r->head_pos += (uint32_t)n;
    return r->f.write(r->f.ctx, off, encoded, (uint32_t)n);
}

int yg_ring_clear_sector(yg_ring_t *r, uint32_t sector) {
    uint32_t seq, trip;
    if (!read_header(r, sector, &seq, &trip)) {
        return 0;
    }
    return r->f.erase(r->f.ctx, sector_offset(sector)) == 0 ? 1 : -1;
}

void yg_ring_reset(yg_ring_t *r) {
    r->any = false;
    r->head = 0;
    r->head_pos = 0;
    r->head_seq = 0;
}

int yg_ring_clear(yg_ring_t *r) {
    int result = 0;
    for (uint32_t s = 0; s < r->sectors; s++) {
        if (yg_ring_clear_sector(r, s) < 0) {
            result = -1;
        }
    }
    yg_ring_reset(r);
    return result;
}

bool yg_ring_sector_at(const yg_ring_t *r, uint32_t age, uint32_t *sector, uint32_t *first_trip) {
    if (!r->any || age >= r->sectors) {
        return false;
    }
    uint32_t s = (r->head + r->sectors - age) % r->sectors;
    uint32_t seq, trip;
    if (!read_header(r, s, &seq, &trip) || seq != r->head_seq - age) {
        return false;
    }
    *sector = s;
    *first_trip = trip;
    return true;
}

uint32_t yg_ring_age_for_trip(const yg_ring_t *r, uint32_t trip) {
    uint32_t age = 0;
    uint32_t sector, first_trip;
    if (!yg_ring_sector_at(r, 0, &sector, &first_trip)) {
        return 0;
    }
    while (first_trip >= trip && yg_ring_sector_at(r, age + 1, &sector, &first_trip)) {
        age++;
    }
    return age;
}

bool yg_ring_newest_seq(const yg_ring_t *r, uint32_t *seq) {
    if (!r->any) {
        return false;
    }
    *seq = r->head_seq;
    return true;
}

bool yg_ring_oldest_seq(const yg_ring_t *r, uint32_t *seq) {
    uint32_t age = 0;
    uint32_t sector, first_trip;
    if (!yg_ring_sector_at(r, 0, &sector, &first_trip)) {
        return false;
    }
    while (yg_ring_sector_at(r, age + 1, &sector, &first_trip)) {
        age++;
    }
    *seq = r->head_seq - age;
    return true;
}

bool yg_ring_sector_for_seq(const yg_ring_t *r, uint32_t seq, uint32_t *sector, uint32_t *first_trip) {
    if (!r->any || (int32_t)(r->head_seq - seq) < 0) {
        return false;
    }
    return yg_ring_sector_at(r, r->head_seq - seq, sector, first_trip);
}

int yg_ring_read_sector(const yg_ring_t *r, uint32_t sector, uint8_t *buf) {
    return r->f.read(r->f.ctx, sector_offset(sector), buf, YG_RING_SECTOR);
}
