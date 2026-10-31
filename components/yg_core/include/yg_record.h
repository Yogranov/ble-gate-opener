/* Log records: how they look in flash and as JSON. See the design spec, section 9.2. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "yg_settings.h"
#include "yg_types.h"

#define YG_RECORD_MAX_PAYLOAD 240
#define YG_RECORD_MAX_ENCODED 256
#define YG_RECORD_JSON_MAX    1536
#define YG_TRACE_SAMPLE_SIZE  5
#define YG_TRACE_MAX_SAMPLES  40
#define YG_CAP_IMU_SAMPLE     12    /* Six 16-bit raw values. */
#define YG_CAP_ADV_ENTRY      5     /* Time 32-bit, RSSI 8-bit. */
#define YG_CAP_IMU_PER_RECORD 19
#define YG_CAP_ADV_PER_RECORD 40

typedef enum
{
    YG_REC_BOOT = 1,
    YG_REC_FIRST_HEARD,
    YG_REC_MOTION,
    YG_REC_PRESS,
    YG_REC_REARM,
    YG_REC_HOTSPOT,
    YG_REC_PAIR,
    YG_REC_SETTING,
    YG_REC_OTA,
    YG_REC_ERROR,
    YG_REC_TRACE,
    YG_REC_TEMP,
    YG_REC_RATING,
    YG_REC_TIME_ANCHOR,
    YG_REC_ENV,
    YG_REC_CAP_START,
    YG_REC_CAP_IMU,
    YG_REC_CAP_ADV,
    YG_REC_CAP_END,
} yg_record_type_t;

typedef enum
{
    YG_PAIR_EVENT_SAVED = 1,
    YG_PAIR_EVENT_TIMEOUT,
    YG_PAIR_EVENT_FORGET,
} yg_pair_event_t;

typedef enum
{
    YG_OTA_EVENT_UPLOADED = 1,
    YG_OTA_EVENT_FAILED,
    YG_OTA_EVENT_CONFIRMED,
} yg_ota_event_t;

typedef enum
{
    YG_ERROR_SENSOR_ABSENT = 1,
    YG_ERROR_SENSOR_GRAVITY,
    YG_ERROR_SENSOR_READS,
    YG_ERROR_SENSOR_STUCK,
    YG_ERROR_SENSOR_RECOVERED,
    YG_ERROR_QUEUE_DROPPED,
} yg_error_code_t;

typedef enum
{
    YG_RATING_GOOD = 1,
    YG_RATING_TOO_EARLY,
    YG_RATING_TOO_LATE,
    YG_RATING_DID_NOT_OPEN,
    YG_RATING_NO_REASON,
} yg_rating_t;

typedef enum
{
    YG_CAP_CAUSE_MOTION = 1,
    YG_CAP_CAUSE_PRESS,
    YG_CAP_CAUSE_NEAR_MISS,
    YG_CAP_CAUSE_MANUAL,
} yg_cap_cause_t;

typedef struct
{
    uint16_t ble_devices;
    uint16_t wifi_networks;
    int8_t wifi_strongest;
    uint8_t per_channel[13];
} yg_env_t;

typedef struct
{
    uint8_t type;
    uint8_t len;
    uint32_t trip;
    uint32_t uptime_ms;
    uint8_t payload[YG_RECORD_MAX_PAYLOAD];
} yg_record_t;

/* Writes the flash form, padded to 4 bytes. `out` must hold YG_RECORD_MAX_ENCODED. */
size_t yg_record_encode(const yg_record_t *r, uint8_t *out);

/* Reads the next valid record from a sector, skipping torn or blank space. */
bool yg_record_next(const uint8_t *data, size_t size, size_t *pos, yg_record_t *out);

/* Writes one JSON object with no newline. Returns the length, or -1 if too small. */
int yg_record_to_json(const yg_record_t *r, char *buf, size_t len);

const char *yg_rating_name(uint8_t rating);
uint8_t yg_rating_parse(const char *name);   /* 0 if unknown. */
const char *yg_press_reason_name(uint8_t reason);
const char *yg_error_name(uint8_t code);

void yg_rec_boot(yg_record_t *r, uint32_t trip, uint32_t now, const char *fw, uint8_t reset_reason,
                 uint8_t sensor_id, const yg_beacon_t *beacon, const yg_settings_t *s);
void yg_rec_first_heard(yg_record_t *r, uint32_t trip, uint32_t now, int8_t rssi);
void yg_rec_motion(yg_record_t *r, uint32_t trip, uint32_t now, uint8_t cause, uint16_t mg, float yaw_deg);
void yg_rec_press(yg_record_t *r, uint32_t trip, uint32_t now, uint8_t reason, int8_t rssi,
                  uint32_t since_motion_ms, int8_t set_point);
void yg_rec_rearm(yg_record_t *r, uint32_t trip, uint32_t now, uint8_t reason);
void yg_rec_hotspot(yg_record_t *r, uint32_t trip, uint32_t now, bool on, uint8_t clients);
void yg_rec_pair(yg_record_t *r, uint32_t trip, uint32_t now, uint8_t event, const yg_addr_t *addr);
void yg_rec_setting(yg_record_t *r, uint32_t trip, uint32_t now, const char *key, const char *value);
void yg_rec_ota(yg_record_t *r, uint32_t trip, uint32_t now, uint8_t event);
void yg_rec_error(yg_record_t *r, uint32_t trip, uint32_t now, uint8_t code, uint32_t detail);
void yg_rec_temp(yg_record_t *r, uint32_t trip, uint32_t now, float celsius);
void yg_rec_rating(yg_record_t *r, uint32_t trip, uint32_t now, uint32_t rated_trip, uint8_t rating);
void yg_rec_time_anchor(yg_record_t *r, uint32_t trip, uint32_t now, uint32_t epoch_s);
void yg_rec_env(yg_record_t *r, uint32_t trip, uint32_t now, const yg_env_t *env);
void yg_rec_cap_start(yg_record_t *r, uint32_t trip, uint32_t now, uint32_t id, uint8_t cause);
void yg_rec_cap_imu(yg_record_t *r, uint32_t trip, uint32_t now, uint32_t id, uint32_t first_index,
                    uint8_t count, const int16_t *raw);
void yg_rec_cap_adv(yg_record_t *r, uint32_t trip, uint32_t now, uint32_t id, uint8_t count,
                    const uint8_t *entries);
void yg_rec_cap_end(yg_record_t *r, uint32_t trip, uint32_t now, uint32_t id, uint32_t samples, uint32_t adverts);

/* A trace record holds up to YG_TRACE_MAX_SAMPLES five-byte samples. */
void yg_rec_trace_begin(yg_record_t *r, uint32_t trip, uint32_t now, uint8_t period_s);
void yg_rec_trace_add(yg_record_t *r, int8_t rssi, uint8_t adverts, uint16_t accel_mg, uint16_t yaw_dps, uint8_t flags);
uint8_t yg_rec_trace_count(const yg_record_t *r);

/* Little-endian access and the checksum, also used by the ring and trip summary. */
uint16_t yg_get_u16(const uint8_t *p);
uint32_t yg_get_u32(const uint8_t *p);
uint16_t yg_crc16(const uint8_t *data, size_t len);
