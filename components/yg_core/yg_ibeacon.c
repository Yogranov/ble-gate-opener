#include "yg_ibeacon.h"

#include <stdio.h>
#include <string.h>

#define AD_MANUFACTURER_DATA 0xFF
#define IBEACON_DATA_LEN     25    /* company 2 + type 2 + uuid 16 + major 2 + minor 2 + power 1 */

bool yg_ibeacon_parse(const uint8_t *adv, size_t len, yg_ibeacon_t *out) {
    memset(out, 0, sizeof(*out));
    size_t pos = 0;
    while (adv != NULL && pos + 1 < len) {
        size_t field_len = adv[pos];
        if (field_len == 0 || pos + 1 + field_len > len) {
            return false;
        }
        const uint8_t *data = &adv[pos + 2];
        size_t data_len = field_len - 1;
        if (adv[pos + 1] == AD_MANUFACTURER_DATA && data_len >= IBEACON_DATA_LEN
            && data[0] == 0x4C && data[1] == 0x00 && data[2] == 0x02 && data[3] == 0x15) {
            memcpy(out->uuid, &data[4], 16);
            out->major = (uint16_t)((data[20] << 8) | data[21]);
            out->minor = (uint16_t)((data[22] << 8) | data[23]);
            out->tx_power = (int8_t)data[24];
            out->valid = true;
            return true;
        }
        pos += field_len + 1;
    }
    return false;
}

void yg_ibeacon_uuid_str(const yg_ibeacon_t *ib, char out[37]) {
    const uint8_t *u = ib->uuid;
    snprintf(out, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
}

bool yg_addr_is_stable(const yg_addr_t *a) {
    if (a->type == YG_ADDR_TYPE_RANDOM) {
        return (a->addr[0] & 0xC0) == 0xC0;
    }
    return true;
}

bool yg_addr_can_pair(const yg_addr_t *a) {
    bool known = a->type == YG_ADDR_TYPE_PUBLIC || a->type == YG_ADDR_TYPE_RANDOM || a->type == YG_ADDR_TYPE_ANY;
    return known && yg_addr_is_stable(a);
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool yg_addr_parse(const char *text, uint8_t out[YG_ADDR_LEN]) {
    if (strlen(text) != 17) {
        return false;
    }
    for (int i = 0; i < YG_ADDR_LEN; i++) {
        int hi = hex_digit(text[i * 3]);
        int lo = hex_digit(text[i * 3 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        if (i < YG_ADDR_LEN - 1 && text[i * 3 + 2] != ':' && text[i * 3 + 2] != '-') {
            return false;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

void yg_addr_format(const uint8_t addr[YG_ADDR_LEN], char out[18]) {
    snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

bool yg_addr_equal(const yg_addr_t *a, const yg_addr_t *b) {
    if (memcmp(a->addr, b->addr, YG_ADDR_LEN) != 0) {
        return false;
    }
    return a->type == b->type || a->type == YG_ADDR_TYPE_ANY || b->type == YG_ADDR_TYPE_ANY;
}
