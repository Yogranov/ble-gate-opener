/* Every user setting, described by one table so that storage, validation and
 * the web page all stay in step. Ranges come from the design spec, section 5.7. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define YG_SET_POINT_ANY 0   /* A set point of 0 means "any advert counts". */
#define YG_SSID_MAX      32
#define YG_PASSWORD_MIN  8
#define YG_PASSWORD_MAX  63
#define YG_PASSWORD_DEFAULT "password"

typedef enum
{
    YG_BUZZER_PASSIVE,
    YG_BUZZER_ACTIVE,
} yg_buzzer_type_t;

typedef struct
{
    int8_t set_point_dbm;
    uint16_t press_ms;
    uint16_t moved_window_s;
    uint16_t gone_s;
    uint16_t still_s;
    int8_t fallback_dbm;
    bool motion_enabled;
    uint16_t motion_accel_mg;
    uint16_t motion_accel_ms;
    uint8_t motion_yaw_deg;
    uint16_t motion_yaw_window_ms;
    int8_t pair_min_dbm;
    uint8_t pair_margin_db;
    uint8_t pair_hold_s;
    uint16_t pair_timeout_s;
    uint16_t hotspot_s;
    char hotspot_ssid[YG_SSID_MAX + 1];
    char hotspot_password[YG_PASSWORD_MAX + 1];
    uint8_t wifi_tx_dbm;
    bool led_enabled;
    bool buzzer_enabled;
    uint8_t buzzer_type;
    bool debug;
} yg_settings_t;

typedef enum
{
    YG_SET_I8,
    YG_SET_U8,
    YG_SET_U16,
    YG_SET_BOOL,
    YG_SET_STR,
    YG_SET_PASSWORD,
    YG_SET_BUZZER_TYPE,
    YG_SET_DBM_OR_ANY,
} yg_setting_type_t;

typedef struct
{
    const char *key;   /* JSON name. */
    const char *nvs;   /* NVS key, at most 15 characters. */
    yg_setting_type_t type;
    uint16_t offset;
    int32_t min;
    int32_t max;
    int32_t def;
} yg_setting_desc_t;

void yg_settings_defaults(yg_settings_t *s);
const yg_setting_desc_t *yg_settings_table(size_t *count);
const yg_setting_desc_t *yg_settings_find(const char *key);

/* Reads a numeric, boolean or enum setting as an integer. */
int32_t yg_settings_get_int(const yg_settings_t *s, const yg_setting_desc_t *d);

/* Both setters return false, leaving the setting unchanged, when the value is
 * out of range or the key is unknown. */
bool yg_settings_set_int(yg_settings_t *s, const char *key, int32_t value);
bool yg_settings_set_str(yg_settings_t *s, const char *key, const char *value);

/* Writes a setting's value as text that yg_settings_set_str reads back. Returns the
 * length, or -1 if buf is too small. */
int yg_settings_format(const yg_settings_t *s, const yg_setting_desc_t *d, char *buf, size_t len);

/* Writes {"settings":[{key,type,value,min,max},...]}. Returns the length, or
 * -1 if buf is too small. The password value is written only when asked for. */
int yg_settings_to_json(const yg_settings_t *s, char *buf, size_t len, bool include_password);
