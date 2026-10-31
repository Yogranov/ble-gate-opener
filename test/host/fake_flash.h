/* In-memory flash that behaves like NOR: writes can only clear bits, erase sets
 * a whole sector to 0xFF, and power can be cut part way through a write. */
#pragma once

#include <stdint.h>

#include "yg_ring.h"

#define FAKE_FLASH_NO_CUT 0xFFFFFFFFu

void fake_flash_init(yg_ring_flash_t *f, uint32_t sectors);

/* After this many more bytes are written, writes stop and report failure. */
void fake_flash_cut_after(uint32_t bytes);
void fake_flash_cut_erase_after(uint32_t bytes);

uint8_t *fake_flash_data(void);
uint32_t fake_flash_erase_count(void);
