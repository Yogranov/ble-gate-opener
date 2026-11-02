#include "yg_nvs.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NAMESPACE "yogigate"
#define KEY_BEACON "beacon"
#define KEY_TRIP   "trip"
#define KEY_GYRO_ZERO "gyro_zero"
#define KEY_TEST_FLAG "test_fail"

static const char *TAG = "yg_nvs";

esp_err_t yg_nvs_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erasing: %s", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static void load_setting(nvs_handle_t h, yg_settings_t *s, const yg_setting_desc_t *d) {
    esp_err_t err = ESP_FAIL;
    int32_t value = 0;
    if (d->type == YG_SET_STR || d->type == YG_SET_PASSWORD) {
        char text[YG_PASSWORD_MAX + 1];
        size_t len = sizeof(text);
        if (nvs_get_str(h, d->nvs, text, &len) == ESP_OK) {
            yg_settings_set_str(s, d->key, text);
        }
        return;
    }
    if (d->type == YG_SET_I8 || d->type == YG_SET_DBM_OR_ANY) {
        int8_t v;
        err = nvs_get_i8(h, d->nvs, &v);
        value = v;
    } else if (d->type == YG_SET_U16) {
        uint16_t v;
        err = nvs_get_u16(h, d->nvs, &v);
        value = v;
    } else {
        uint8_t v;
        err = nvs_get_u8(h, d->nvs, &v);
        value = v;
    }
    if (err == ESP_OK && !yg_settings_set_int(s, d->key, value)) {
        ESP_LOGW(TAG, "Ignoring out of range %s=%d", d->key, (int)value);
    }
}

void yg_nvs_default_ssid(char *out, size_t len) {
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(out, len, "YogiGate-%02X%02X", mac[4], mac[5]);
}

static void create_identity(yg_settings_t *s) {
    if (s->hotspot_ssid[0] == 0) {
        yg_nvs_default_ssid(s->hotspot_ssid, sizeof(s->hotspot_ssid));
    }
}

bool yg_nvs_load(yg_settings_t *s, yg_beacon_t *b) {
    yg_settings_defaults(s);
    memset(b, 0, sizeof(*b));
    nvs_handle_t h;
    bool opened = nvs_open(NAMESPACE, NVS_READWRITE, &h) == ESP_OK;
    if (opened) {
        size_t count = 0;
        const yg_setting_desc_t *table = yg_settings_table(&count);
        for (size_t i = 0; i < count; i++) {
            load_setting(h, s, &table[i]);
        }
        size_t len = sizeof(*b);
        if (nvs_get_blob(h, KEY_BEACON, b, &len) != ESP_OK || len != sizeof(*b)) {
            memset(b, 0, sizeof(*b));
        }
        nvs_close(h);
    }
    bool new_identity = s->hotspot_ssid[0] == 0;
    create_identity(s);
    if (new_identity) {
        yg_nvs_save_settings(s);
    }
    return opened;
}

void yg_nvs_save_settings(const yg_settings_t *s) {
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    size_t count = 0;
    const yg_setting_desc_t *table = yg_settings_table(&count);
    for (size_t i = 0; i < count; i++) {
        const yg_setting_desc_t *d = &table[i];
        if (d->type == YG_SET_STR || d->type == YG_SET_PASSWORD) {
            const char *text = (const char *)s + d->offset;
            char stored[YG_PASSWORD_MAX + 1];
            size_t len = sizeof(stored);
            if (nvs_get_str(h, d->nvs, stored, &len) != ESP_OK || strcmp(stored, text) != 0) {
                nvs_set_str(h, d->nvs, text);
            }
            continue;
        }
        int32_t value = yg_settings_get_int(s, d);
        if (d->type == YG_SET_I8 || d->type == YG_SET_DBM_OR_ANY) {
            int8_t stored;
            if (nvs_get_i8(h, d->nvs, &stored) != ESP_OK || stored != value) {
                nvs_set_i8(h, d->nvs, (int8_t)value);
            }
        } else if (d->type == YG_SET_U16) {
            uint16_t stored;
            if (nvs_get_u16(h, d->nvs, &stored) != ESP_OK || stored != value) {
                nvs_set_u16(h, d->nvs, (uint16_t)value);
            }
        } else {
            uint8_t stored;
            if (nvs_get_u8(h, d->nvs, &stored) != ESP_OK || stored != value) {
                nvs_set_u8(h, d->nvs, (uint8_t)value);
            }
        }
    }
    nvs_commit(h);
    nvs_close(h);
}

void yg_nvs_save_beacon(const yg_beacon_t *b) {
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, KEY_BEACON, b, sizeof(*b));
        nvs_commit(h);
        nvs_close(h);
    }
}

bool yg_nvs_load_gyro_zero(float zero[3]) {
    bool found = false;
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(float) * 3;
        found = nvs_get_blob(h, KEY_GYRO_ZERO, zero, &len) == ESP_OK && len == sizeof(float) * 3;
        nvs_close(h);
    }
    return found;
}

void yg_nvs_save_gyro_zero(const float zero[3]) {
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, KEY_GYRO_ZERO, zero, sizeof(float) * 3);
        nvs_commit(h);
        nvs_close(h);
    }
}

uint32_t yg_nvs_next_trip(void) {
    uint32_t trip = 0;
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_get_u32(h, KEY_TRIP, &trip);
        trip++;
        nvs_set_u32(h, KEY_TRIP, trip);
        nvs_commit(h);
        nvs_close(h);
    }
    return trip;
}

void yg_nvs_set_trip(uint32_t trip) {
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, KEY_TRIP, trip);
        nvs_commit(h);
        nvs_close(h);
    }
}

esp_err_t yg_nvs_factory_reset(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_all(h);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

void yg_nvs_set_test_flag(void) {
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, KEY_TEST_FLAG, 1);
        nvs_commit(h);
        nvs_close(h);
    }
}

bool yg_nvs_take_test_flag(void) {
    uint8_t value = 0;
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_get_u8(h, KEY_TEST_FLAG, &value) == ESP_OK) {
            nvs_erase_key(h, KEY_TEST_FLAG);
            nvs_commit(h);
        }
        nvs_close(h);
    }
    return value != 0;
}
