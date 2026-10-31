#include "yg_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yg_json.h"

#define FIELD(name) ((uint16_t)offsetof(yg_settings_t, name))

static const yg_setting_desc_t TABLE[] = {
    { "set_point_dbm", "set_point", YG_SET_DBM_OR_ANY, FIELD(set_point_dbm), -100, -30, YG_SET_POINT_ANY },
    { "press_ms", "press_ms", YG_SET_U16, FIELD(press_ms), 200, 5000, 2000 },
    { "moved_window_s", "moved_win", YG_SET_U16, FIELD(moved_window_s), 10, 300, 60 },
    { "gone_s", "gone_s", YG_SET_U16, FIELD(gone_s), 10, 600, 60 },
    { "still_s", "still_s", YG_SET_U16, FIELD(still_s), 30, 1800, 120 },
    { "fallback_dbm", "fallback", YG_SET_I8, FIELD(fallback_dbm), -100, -30, -90 },
    { "motion_enabled", "motion_en", YG_SET_BOOL, FIELD(motion_enabled), 0, 1, 1 },
    { "motion_accel_mg", "accel_mg", YG_SET_U16, FIELD(motion_accel_mg), 10, 300, 40 },
    { "motion_accel_ms", "accel_ms", YG_SET_U16, FIELD(motion_accel_ms), 200, 3000, 800 },
    { "motion_yaw_deg", "yaw_deg", YG_SET_U8, FIELD(motion_yaw_deg), 5, 90, 15 },
    { "motion_yaw_window_ms", "yaw_win_ms", YG_SET_U16, FIELD(motion_yaw_window_ms), 1000, 10000, 3000 },
    { "pair_min_dbm", "pair_min", YG_SET_I8, FIELD(pair_min_dbm), -80, -20, -45 },
    { "pair_margin_db", "pair_margin", YG_SET_U8, FIELD(pair_margin_db), 3, 40, 15 },
    { "pair_hold_s", "pair_hold", YG_SET_U8, FIELD(pair_hold_s), 1, 10, 2 },
    { "pair_timeout_s", "pair_timeout", YG_SET_U16, FIELD(pair_timeout_s), 10, 300, 60 },
    { "hotspot_s", "hotspot_s", YG_SET_U16, FIELD(hotspot_s), 0, 3600, 300 },
    { "hotspot_ssid", "ssid", YG_SET_STR, FIELD(hotspot_ssid), 1, YG_SSID_MAX, 0 },
    { "hotspot_password", "password", YG_SET_PASSWORD, FIELD(hotspot_password), YG_PASSWORD_MIN, YG_PASSWORD_MAX, 0 },
    { "wifi_tx_dbm", "wifi_tx", YG_SET_U8, FIELD(wifi_tx_dbm), 2, 20, 8 },
    { "led_enabled", "led_en", YG_SET_BOOL, FIELD(led_enabled), 0, 1, 1 },
    { "buzzer_enabled", "buzzer_en", YG_SET_BOOL, FIELD(buzzer_enabled), 0, 1, 0 },
    { "buzzer_type", "buzzer_type", YG_SET_BUZZER_TYPE, FIELD(buzzer_type), YG_BUZZER_PASSIVE, YG_BUZZER_ACTIVE, YG_BUZZER_PASSIVE },
    { "debug", "debug", YG_SET_BOOL, FIELD(debug), 0, 1, 0 },
};

#define TABLE_COUNT (sizeof(TABLE) / sizeof(TABLE[0]))

static void store_int(yg_settings_t *s, const yg_setting_desc_t *d, int32_t value) {
    void *field = (char *)s + d->offset;
    switch (d->type) {
    case YG_SET_I8:
    case YG_SET_DBM_OR_ANY:
        *(int8_t *)field = (int8_t)value;
        break;
    case YG_SET_U8:
    case YG_SET_BUZZER_TYPE:
        *(uint8_t *)field = (uint8_t)value;
        break;
    case YG_SET_U16:
        *(uint16_t *)field = (uint16_t)value;
        break;
    case YG_SET_BOOL:
        *(bool *)field = value != 0;
        break;
    default:
        break;
    }
}

void yg_settings_defaults(yg_settings_t *s) {
    memset(s, 0, sizeof(*s));
    for (size_t i = 0; i < TABLE_COUNT; i++) {
        store_int(s, &TABLE[i], TABLE[i].def);
    }
}

const yg_setting_desc_t *yg_settings_table(size_t *count) {
    *count = TABLE_COUNT;
    return TABLE;
}

const yg_setting_desc_t *yg_settings_find(const char *key) {
    for (size_t i = 0; i < TABLE_COUNT; i++) {
        if (strcmp(TABLE[i].key, key) == 0) {
            return &TABLE[i];
        }
    }
    return NULL;
}

int32_t yg_settings_get_int(const yg_settings_t *s, const yg_setting_desc_t *d) {
    const void *field = (const char *)s + d->offset;
    switch (d->type) {
    case YG_SET_I8:
    case YG_SET_DBM_OR_ANY:
        return *(const int8_t *)field;
    case YG_SET_U8:
    case YG_SET_BUZZER_TYPE:
        return *(const uint8_t *)field;
    case YG_SET_U16:
        return *(const uint16_t *)field;
    case YG_SET_BOOL:
        return *(const bool *)field ? 1 : 0;
    default:
        return 0;
    }
}

bool yg_settings_set_int(yg_settings_t *s, const char *key, int32_t value) {
    const yg_setting_desc_t *d = yg_settings_find(key);
    if (d == NULL || d->type == YG_SET_STR || d->type == YG_SET_PASSWORD) {
        return false;
    }
    bool any = d->type == YG_SET_DBM_OR_ANY && value == YG_SET_POINT_ANY;
    if (!any && (value < d->min || value > d->max)) {
        return false;
    }
    store_int(s, d, value);
    return true;
}

bool yg_settings_set_str(yg_settings_t *s, const char *key, const char *value) {
    const yg_setting_desc_t *d = yg_settings_find(key);
    if (d == NULL) {
        return false;
    }
    if (d->type == YG_SET_STR || d->type == YG_SET_PASSWORD) {
        size_t n = strlen(value);
        if (n < (size_t)d->min || n > (size_t)d->max) {
            return false;
        }
        memcpy((char *)s + d->offset, value, n + 1);
        return true;
    }
    if (d->type == YG_SET_BUZZER_TYPE) {
        if (strcmp(value, "passive") == 0) {
            return yg_settings_set_int(s, key, YG_BUZZER_PASSIVE);
        }
        if (strcmp(value, "active") == 0) {
            return yg_settings_set_int(s, key, YG_BUZZER_ACTIVE);
        }
        return false;
    }
    if (d->type == YG_SET_DBM_OR_ANY && strcmp(value, "any") == 0) {
        return yg_settings_set_int(s, key, YG_SET_POINT_ANY);
    }
    char *end = NULL;
    long number = strtol(value, &end, 10);
    if (end == value || *end != 0) {
        return false;
    }
    return yg_settings_set_int(s, key, (int32_t)number);
}

int yg_settings_to_json(const yg_settings_t *s, char *buf, size_t len, bool include_password) {
    yg_jw_t w;
    yg_jw_init(&w, buf, len);
    yg_jw_raw(&w, "{\"settings\":[");
    for (size_t i = 0; i < TABLE_COUNT; i++) {
        const yg_setting_desc_t *d = &TABLE[i];
        const char *text = (const char *)s + d->offset;
        int32_t value = yg_settings_get_int(s, d);
        yg_jw_raw(&w, "%s{\"key\":\"%s\",", i ? "," : "", d->key);
        switch (d->type) {
        case YG_SET_BOOL:
            yg_jw_raw(&w, "\"type\":\"bool\",\"value\":%s}", value ? "true" : "false");
            break;
        case YG_SET_STR:
            yg_jw_raw(&w, "\"type\":\"string\",\"value\":");
            yg_jw_string(&w, text);
            yg_jw_raw(&w, ",\"min\":%d,\"max\":%d}", (int)d->min, (int)d->max);
            break;
        case YG_SET_PASSWORD:
            yg_jw_raw(&w, "\"type\":\"password\",");
            if (include_password) {
                yg_jw_raw(&w, "\"value\":");
                yg_jw_string(&w, text);
                yg_jw_raw(&w, ",");
            }
            yg_jw_raw(&w, "\"min\":%d,\"max\":%d}", (int)d->min, (int)d->max);
            break;
        case YG_SET_BUZZER_TYPE:
            yg_jw_raw(&w, "\"type\":\"enum\",\"value\":\"%s\",\"options\":[\"passive\",\"active\"]}",
                      value == YG_BUZZER_ACTIVE ? "active" : "passive");
            break;
        case YG_SET_DBM_OR_ANY:
            if (value == YG_SET_POINT_ANY) {
                yg_jw_raw(&w, "\"type\":\"dbm_or_any\",\"value\":\"any\",");
            } else {
                yg_jw_raw(&w, "\"type\":\"dbm_or_any\",\"value\":%d,", (int)value);
            }
            yg_jw_raw(&w, "\"min\":%d,\"max\":%d}", (int)d->min, (int)d->max);
            break;
        default:
            yg_jw_raw(&w, "\"type\":\"int\",\"value\":%d,\"min\":%d,\"max\":%d}", (int)value, (int)d->min, (int)d->max);
            break;
        }
    }
    yg_jw_raw(&w, "]}");
    return yg_jw_finish(&w);
}
