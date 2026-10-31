#include "fake_flash.h"

#include <stdlib.h>
#include <string.h>

static uint8_t *data;
static uint32_t total;
static uint32_t cut_after = FAKE_FLASH_NO_CUT;
static uint32_t cut_erase = FAKE_FLASH_NO_CUT;
static uint32_t erases;

static int fake_read(void *ctx, uint32_t off, void *buf, uint32_t len) {
    (void)ctx;
    if (off + len > total) return -1;
    memcpy(buf, data + off, len);
    return 0;
}

static int fake_write(void *ctx, uint32_t off, const void *buf, uint32_t len) {
    (void)ctx;
    if (off + len > total) return -1;
    const uint8_t *src = buf;
    for (uint32_t i = 0; i < len; i++) {
        if (cut_after == 0) return -1;
        data[off + i] &= src[i];
        if (cut_after != FAKE_FLASH_NO_CUT) cut_after--;
    }
    return 0;
}

static int fake_erase(void *ctx, uint32_t off) {
    (void)ctx;
    if (off + YG_RING_SECTOR > total || off % YG_RING_SECTOR) return -1;
    erases++;
    if (cut_erase != FAKE_FLASH_NO_CUT) {
        uint32_t n = cut_erase < YG_RING_SECTOR ? cut_erase : YG_RING_SECTOR;
        memset(data + off + YG_RING_SECTOR - n, 0xFF, n);
        cut_erase = FAKE_FLASH_NO_CUT;
        return -1;
    }
    memset(data + off, 0xFF, YG_RING_SECTOR);
    return 0;
}

void fake_flash_init(yg_ring_flash_t *f, uint32_t sectors) {
    free(data);
    total = sectors * YG_RING_SECTOR;
    data = malloc(total);
    memset(data, 0xFF, total);
    cut_after = FAKE_FLASH_NO_CUT;
    cut_erase = FAKE_FLASH_NO_CUT;
    erases = 0;
    f->read = fake_read;
    f->write = fake_write;
    f->erase = fake_erase;
    f->ctx = NULL;
    f->size = total;
}

void fake_flash_cut_after(uint32_t bytes) {
    cut_after = bytes;
}

void fake_flash_cut_erase_after(uint32_t bytes) {
    cut_erase = bytes;
}

uint8_t *fake_flash_data(void) {
    return data;
}

uint32_t fake_flash_erase_count(void) {
    return erases;
}
