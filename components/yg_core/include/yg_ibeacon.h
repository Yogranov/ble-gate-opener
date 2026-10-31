/* iBeacon frames and BLE addresses. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "yg_types.h"

typedef struct
{
    bool valid;
    uint8_t uuid[16];
    uint16_t major;
    uint16_t minor;
    int8_t tx_power;
} yg_ibeacon_t;

/* Parses raw advertising data. Returns true and fills `out` for an iBeacon frame. */
bool yg_ibeacon_parse(const uint8_t *adv, size_t len, yg_ibeacon_t *out);
void yg_ibeacon_uuid_str(const yg_ibeacon_t *ib, char out[37]);

/* Public and random static addresses stay the same. Phones rotate theirs. */
bool yg_addr_is_stable(const yg_addr_t *a);

/* True if a beacon with this address could be heard again later: the type is one the
 * radio reports, or unknown because the MAC was typed, and the address does not rotate. */
bool yg_addr_can_pair(const yg_addr_t *a);
bool yg_addr_parse(const char *text, uint8_t out[YG_ADDR_LEN]);
void yg_addr_format(const uint8_t addr[YG_ADDR_LEN], char out[18]);

/* Compares addresses, ignoring the type if either side is YG_ADDR_TYPE_ANY. */
bool yg_addr_equal(const yg_addr_t *a, const yg_addr_t *b);
