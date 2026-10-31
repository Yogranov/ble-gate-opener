/* Small types shared across the logic modules. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define YG_ADDR_LEN 6

#define YG_ADDR_TYPE_PUBLIC 0
#define YG_ADDR_TYPE_RANDOM 1
#define YG_ADDR_TYPE_ANY    0xFF   /* A typed MAC: the address type is unknown. */

/* addr[0] is the first octet as written, so c5:b0:... has addr[0] == 0xC5. */
typedef struct
{
    uint8_t addr[YG_ADDR_LEN];
    uint8_t type;
} yg_addr_t;

typedef struct
{
    bool paired;
    yg_addr_t addr;
} yg_beacon_t;
