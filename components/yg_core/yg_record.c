#include "yg_record.h"

#include <string.h>

#include "yg_ibeacon.h"
#include "yg_json.h"
#include "yg_motion.h"
#include "yg_trigger.h"

#define RECORD_MAGIC  0xA5
#define HEADER_SIZE   12
#define CRC_SIZE      2

static size_t bounded_len(const char *text, size_t max) {
    size_t n = 0;
    while (n < max && text[n] != 0) {
        n++;
    }
    return n;
}

uint16_t yg_get_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

uint32_t yg_get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

uint16_t yg_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)(data[i] << 8);
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

size_t yg_record_encode(const yg_record_t *r, uint8_t *out) {
    out[0] = RECORD_MAGIC;
    out[1] = r->type;
    out[2] = r->len;
    out[3] = 0;
    put_u32(out + 4, r->trip);
    put_u32(out + 8, r->uptime_ms);
    memcpy(out + HEADER_SIZE, r->payload, r->len);
    size_t used = HEADER_SIZE + r->len;
    put_u16(out + used, yg_crc16(out, used));
    used += CRC_SIZE;
    while (used % 4) {
        out[used++] = 0xFF;
    }
    return used;
}

bool yg_record_next(const uint8_t *data, size_t size, size_t *pos, yg_record_t *out) {
    size_t p = *pos;
    while (p + HEADER_SIZE + CRC_SIZE <= size) {
        if (data[p] == RECORD_MAGIC) {
            size_t len = data[p + 2];
            size_t used = HEADER_SIZE + len;
            if (len <= YG_RECORD_MAX_PAYLOAD && p + used + CRC_SIZE <= size
                && yg_get_u16(data + p + used) == yg_crc16(data + p, used)) {
                out->type = data[p + 1];
                out->len = (uint8_t)len;
                out->trip = yg_get_u32(data + p + 4);
                out->uptime_ms = yg_get_u32(data + p + 8);
                memcpy(out->payload, data + p + HEADER_SIZE, len);
                size_t total = used + CRC_SIZE;
                *pos = p + ((total + 3) & ~(size_t)3);
                return true;
            }
        }
        p += 4;
    }
    *pos = size;
    return false;
}

static void begin(yg_record_t *r, uint8_t type, uint32_t trip, uint32_t now, uint8_t len) {
    r->type = type;
    r->len = len;
    r->trip = trip;
    r->uptime_ms = now;
    memset(r->payload, 0, sizeof(r->payload));
}

void yg_rec_boot(yg_record_t *r, uint32_t trip, uint32_t now, const char *fw, uint8_t reset_reason,
                 uint8_t sensor_id, const yg_beacon_t *beacon, const yg_settings_t *s) {
    begin(r, YG_REC_BOOT, trip, now, 45);
    uint8_t *p = r->payload;
    strncpy((char *)p, fw, 15);
    p[16] = reset_reason;
    p[17] = sensor_id;
    p[18] = beacon->paired;
    memcpy(p + 19, beacon->addr.addr, YG_ADDR_LEN);
    p[25] = beacon->addr.type;
    p[26] = s->debug;
    p[27] = (uint8_t)s->set_point_dbm;
    put_u16(p + 28, s->press_ms);
    put_u16(p + 30, s->moved_window_s);
    put_u16(p + 32, s->gone_s);
    put_u16(p + 34, s->still_s);
    p[36] = (uint8_t)s->fallback_dbm;
    p[37] = s->motion_enabled;
    put_u16(p + 38, s->motion_accel_mg);
    put_u16(p + 40, s->motion_accel_ms);
    p[42] = s->motion_yaw_deg;
    put_u16(p + 43, s->motion_yaw_window_ms);
}

void yg_rec_first_heard(yg_record_t *r, uint32_t trip, uint32_t now, int8_t rssi) {
    begin(r, YG_REC_FIRST_HEARD, trip, now, 1);
    r->payload[0] = (uint8_t)rssi;
}

static int16_t to_tenths(float v) {
    return (int16_t)(v * 10.0f + (v >= 0 ? 0.5f : -0.5f));
}

void yg_rec_motion(yg_record_t *r, uint32_t trip, uint32_t now, uint8_t cause, uint16_t mg, float yaw_deg) {
    begin(r, YG_REC_MOTION, trip, now, 5);
    r->payload[0] = cause;
    put_u16(r->payload + 1, mg);
    put_u16(r->payload + 3, (uint16_t)to_tenths(yaw_deg));
}

void yg_rec_press(yg_record_t *r, uint32_t trip, uint32_t now, uint8_t reason, int8_t rssi,
                  uint32_t since_motion_ms, int8_t set_point) {
    begin(r, YG_REC_PRESS, trip, now, 7);
    r->payload[0] = reason;
    r->payload[1] = (uint8_t)rssi;
    put_u32(r->payload + 2, since_motion_ms);
    r->payload[6] = (uint8_t)set_point;
}

void yg_rec_rearm(yg_record_t *r, uint32_t trip, uint32_t now, uint8_t reason) {
    begin(r, YG_REC_REARM, trip, now, 1);
    r->payload[0] = reason;
}

void yg_rec_hotspot(yg_record_t *r, uint32_t trip, uint32_t now, bool on, uint8_t clients) {
    begin(r, YG_REC_HOTSPOT, trip, now, 2);
    r->payload[0] = on;
    r->payload[1] = clients;
}

void yg_rec_pair(yg_record_t *r, uint32_t trip, uint32_t now, uint8_t event, const yg_addr_t *addr) {
    begin(r, YG_REC_PAIR, trip, now, 8);
    r->payload[0] = event;
    if (addr != NULL) {
        memcpy(r->payload + 1, addr->addr, YG_ADDR_LEN);
        r->payload[7] = addr->type;
    }
}

void yg_rec_setting(yg_record_t *r, uint32_t trip, uint32_t now, const char *key, const char *value) {
    begin(r, YG_REC_SETTING, trip, now, 0);
    size_t key_len = bounded_len(key, 63);
    size_t value_len = bounded_len(value, YG_RECORD_MAX_PAYLOAD - key_len - 2);
    memcpy(r->payload, key, key_len);
    memcpy(r->payload + key_len + 1, value, value_len);
    r->len = (uint8_t)(key_len + value_len + 2);
}

void yg_rec_ota(yg_record_t *r, uint32_t trip, uint32_t now, uint8_t event) {
    begin(r, YG_REC_OTA, trip, now, 1);
    r->payload[0] = event;
}

void yg_rec_error(yg_record_t *r, uint32_t trip, uint32_t now, uint8_t code, uint32_t detail) {
    begin(r, YG_REC_ERROR, trip, now, 5);
    r->payload[0] = code;
    put_u32(r->payload + 1, detail);
}

void yg_rec_temp(yg_record_t *r, uint32_t trip, uint32_t now, float celsius) {
    begin(r, YG_REC_TEMP, trip, now, 2);
    put_u16(r->payload, (uint16_t)to_tenths(celsius));
}

void yg_rec_rating(yg_record_t *r, uint32_t trip, uint32_t now, uint32_t rated_trip, uint8_t rating) {
    begin(r, YG_REC_RATING, trip, now, 5);
    put_u32(r->payload, rated_trip);
    r->payload[4] = rating;
}

void yg_rec_time_anchor(yg_record_t *r, uint32_t trip, uint32_t now, uint32_t epoch_s) {
    begin(r, YG_REC_TIME_ANCHOR, trip, now, 4);
    put_u32(r->payload, epoch_s);
}

void yg_rec_env(yg_record_t *r, uint32_t trip, uint32_t now, const yg_env_t *env) {
    begin(r, YG_REC_ENV, trip, now, 19);
    put_u16(r->payload, env->ble_devices);
    put_u16(r->payload + 2, env->wifi_networks);
    r->payload[4] = (uint8_t)env->wifi_strongest;
    memcpy(r->payload + 5, env->per_channel, 13);
    r->payload[18] = env->wifi_scanned;
}

void yg_rec_cap_start(yg_record_t *r, uint32_t trip, uint32_t now, uint32_t id, uint8_t cause, uint16_t pre_samples) {
    begin(r, YG_REC_CAP_START, trip, now, 7);
    put_u32(r->payload, id);
    r->payload[4] = cause;
    put_u16(r->payload + 5, pre_samples);
}

void yg_rec_cap_imu(yg_record_t *r, uint32_t trip, uint32_t now, uint32_t id, uint32_t first_index,
                    uint8_t count, const int16_t *raw) {
    begin(r, YG_REC_CAP_IMU, trip, now, (uint8_t)(9 + count * YG_CAP_IMU_SAMPLE));
    put_u32(r->payload, id);
    put_u32(r->payload + 4, first_index);
    r->payload[8] = count;
    for (int i = 0; i < count * 6; i++) {
        put_u16(r->payload + 9 + i * 2, (uint16_t)raw[i]);
    }
}

void yg_rec_cap_adv(yg_record_t *r, uint32_t trip, uint32_t now, uint32_t id, uint8_t count,
                    const uint8_t *entries) {
    begin(r, YG_REC_CAP_ADV, trip, now, (uint8_t)(5 + count * YG_CAP_ADV_ENTRY));
    put_u32(r->payload, id);
    r->payload[4] = count;
    memcpy(r->payload + 5, entries, (size_t)count * YG_CAP_ADV_ENTRY);
}

void yg_rec_cap_end(yg_record_t *r, uint32_t trip, uint32_t now, uint32_t id, uint32_t samples, uint32_t adverts) {
    begin(r, YG_REC_CAP_END, trip, now, 12);
    put_u32(r->payload, id);
    put_u32(r->payload + 4, samples);
    put_u32(r->payload + 8, adverts);
}

void yg_rec_trace_begin(yg_record_t *r, uint32_t trip, uint32_t now, uint8_t period_s) {
    begin(r, YG_REC_TRACE, trip, now, 2);
    r->payload[0] = period_s;
    r->payload[1] = 0;
}

void yg_rec_trace_add(yg_record_t *r, int8_t rssi, uint8_t adverts, uint16_t accel_mg, uint16_t yaw_dps, uint8_t flags) {
    uint8_t count = r->payload[1];
    if (count >= YG_TRACE_MAX_SAMPLES) {
        return;
    }
    uint8_t *p = r->payload + 2 + count * YG_TRACE_SAMPLE_SIZE;
    p[0] = (uint8_t)rssi;
    p[1] = adverts;
    p[2] = accel_mg / 10 > 255 ? 255 : (uint8_t)(accel_mg / 10);
    p[3] = yaw_dps > 255 ? 255 : (uint8_t)yaw_dps;
    p[4] = flags;
    r->payload[1] = (uint8_t)(count + 1);
    r->len = (uint8_t)(2 + (count + 1) * YG_TRACE_SAMPLE_SIZE);
}

uint8_t yg_rec_trace_count(const yg_record_t *r) {
    return r->payload[1];
}

const char *yg_rating_name(uint8_t rating) {
    switch (rating) {
    case YG_RATING_GOOD: return "good";
    case YG_RATING_TOO_EARLY: return "too_early";
    case YG_RATING_TOO_LATE: return "too_late";
    case YG_RATING_DID_NOT_OPEN: return "did_not_open";
    case YG_RATING_NO_REASON: return "no_reason";
    default: return "unknown";
    }
}

uint8_t yg_rating_parse(const char *name) {
    for (uint8_t i = YG_RATING_GOOD; i <= YG_RATING_NO_REASON; i++) {
        if (strcmp(name, yg_rating_name(i)) == 0) {
            return i;
        }
    }
    return 0;
}

const char *yg_press_reason_name(uint8_t reason) {
    switch (reason) {
    case YG_PRESS_AUTO: return "auto";
    case YG_PRESS_BUTTON: return "button";
    case YG_PRESS_WEB: return "web";
    case YG_PRESS_FALLBACK: return "fallback";
    default: return "none";
    }
}

const char *yg_error_name(uint8_t code) {
    switch (code) {
    case YG_ERROR_SENSOR_ABSENT: return "sensor_absent";
    case YG_ERROR_SENSOR_GRAVITY: return "sensor_gravity";
    case YG_ERROR_SENSOR_READS: return "sensor_reads";
    case YG_ERROR_SENSOR_STUCK: return "sensor_stuck";
    case YG_ERROR_SENSOR_RECOVERED: return "sensor_recovered";
    case YG_ERROR_QUEUE_DROPPED: return "queue_dropped";
    default: return "unknown";
    }
}

static const char *cause_name(uint8_t cause) {
    return cause == YG_MOTION_YAW ? "yaw" : cause == YG_MOTION_ACCEL ? "accel" : "none";
}

static const char *cap_cause_name(uint8_t cause) {
    switch (cause) {
    case YG_CAP_CAUSE_MOTION: return "motion";
    case YG_CAP_CAUSE_PRESS: return "press";
    case YG_CAP_CAUSE_NEAR_MISS: return "near_miss";
    case YG_CAP_CAUSE_MANUAL: return "manual";
    default: return "unknown";
    }
}

static const char *pair_event_name(uint8_t event) {
    switch (event) {
    case YG_PAIR_EVENT_SAVED: return "saved";
    case YG_PAIR_EVENT_TIMEOUT: return "timeout";
    case YG_PAIR_EVENT_FORGET: return "forget";
    default: return "unknown";
    }
}

static const char *ota_event_name(uint8_t event) {
    switch (event) {
    case YG_OTA_EVENT_UPLOADED: return "uploaded";
    case YG_OTA_EVENT_FAILED: return "failed";
    case YG_OTA_EVENT_CONFIRMED: return "confirmed";
    default: return "unknown";
    }
}

static void json_set_point(yg_jw_t *w, int8_t set_point) {
    if (set_point == YG_SET_POINT_ANY) {
        yg_jw_raw(w, "\"any\"");
    } else {
        yg_jw_raw(w, "%d", (int)set_point);
    }
}

static void json_base64(yg_jw_t *w, const uint8_t *data, size_t len) {
    static const char TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = 0; i < len; i += 3) {
        uint32_t chunk = (uint32_t)data[i] << 16;
        if (i + 1 < len) chunk |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) chunk |= data[i + 2];
        yg_jw_raw(w, "%c%c%c%c", TABLE[(chunk >> 18) & 63], TABLE[(chunk >> 12) & 63],
                  i + 1 < len ? TABLE[(chunk >> 6) & 63] : '=', i + 2 < len ? TABLE[chunk & 63] : '=');
    }
}

static void json_body(yg_jw_t *w, const yg_record_t *r) {
    const uint8_t *p = r->payload;
    char mac[18];
    switch (r->type) {
    case YG_REC_BOOT: {
        char fw[17] = { 0 };
        memcpy(fw, p, 16);
        yg_jw_raw(w, "\"k\":\"boot\",\"fw\":");
        yg_jw_string(w, fw);
        yg_jw_raw(w, ",\"reset\":%u,\"sensor_id\":%u,\"beacon\":", p[16], p[17]);
        if (p[18]) {
            yg_addr_format(p + 19, mac);
            yg_jw_raw(w, "\"%s\"", mac);
        } else {
            yg_jw_raw(w, "null");
        }
        yg_jw_raw(w, ",\"beacon_type\":%u,\"debug\":%s,\"set_point\":", p[25], p[26] ? "true" : "false");
        json_set_point(w, (int8_t)p[27]);
        yg_jw_raw(w, ",\"press_ms\":%u,\"moved_window_s\":%u,\"gone_s\":%u,\"still_s\":%u,\"fallback_dbm\":%d,"
                     "\"motion_enabled\":%s,\"motion_accel_mg\":%u,\"motion_accel_ms\":%u,\"motion_yaw_deg\":%u,\"motion_yaw_window_ms\":%u",
                  yg_get_u16(p + 28), yg_get_u16(p + 30), yg_get_u16(p + 32), yg_get_u16(p + 34), (int)(int8_t)p[36],
                  p[37] ? "true" : "false", yg_get_u16(p + 38), yg_get_u16(p + 40), p[42], yg_get_u16(p + 43));
        break;
    }
    case YG_REC_FIRST_HEARD:
        yg_jw_raw(w, "\"k\":\"first_heard\",\"rssi\":%d", (int)(int8_t)p[0]);
        break;
    case YG_REC_MOTION:
        yg_jw_raw(w, "\"k\":\"motion\",\"cause\":\"%s\",\"mg\":%u,\"yaw\":", cause_name(p[0]), yg_get_u16(p + 1));
        yg_jw_tenths(w, (int16_t)yg_get_u16(p + 3));
        break;
    case YG_REC_PRESS:
        yg_jw_raw(w, "\"k\":\"press\",\"reason\":\"%s\",\"rssi\":", yg_press_reason_name(p[0]));
        if (p[1] == 0) {
            yg_jw_raw(w, "null");
        } else {
            yg_jw_raw(w, "%d", (int)(int8_t)p[1]);
        }
        yg_jw_raw(w, ",\"since_motion_ms\":%u,\"set_point\":", (unsigned)yg_get_u32(p + 2));
        json_set_point(w, (int8_t)p[6]);
        break;
    case YG_REC_REARM:
        yg_jw_raw(w, "\"k\":\"rearm\",\"reason\":\"%s\"", p[0] == YG_REARM_GONE ? "gone" : "still");
        break;
    case YG_REC_HOTSPOT:
        yg_jw_raw(w, "\"k\":\"hotspot\",\"on\":%s,\"clients\":%u", p[0] ? "true" : "false", p[1]);
        break;
    case YG_REC_PAIR:
        yg_addr_format(p + 1, mac);
        yg_jw_raw(w, "\"k\":\"pair\",\"event\":\"%s\",\"mac\":\"%s\",\"type\":%u", pair_event_name(p[0]), mac, p[7]);
        break;
    case YG_REC_SETTING: {
        const char *key = (const char *)p;
        size_t key_len = bounded_len(key, r->len);
        yg_jw_raw(w, "\"k\":\"setting\",\"key\":");
        yg_jw_string(w, key);
        yg_jw_raw(w, ",\"value\":");
        yg_jw_string(w, key_len + 1 < r->len ? key + key_len + 1 : "");
        break;
    }
    case YG_REC_OTA:
        yg_jw_raw(w, "\"k\":\"ota\",\"event\":\"%s\"", ota_event_name(p[0]));
        break;
    case YG_REC_ERROR:
        yg_jw_raw(w, "\"k\":\"error\",\"code\":\"%s\",\"detail\":%u", yg_error_name(p[0]), (unsigned)yg_get_u32(p + 1));
        break;
    case YG_REC_TRACE: {
        yg_jw_raw(w, "\"k\":\"trace\",\"period\":%u,\"s\":[", p[0]);
        for (int i = 0; i < p[1]; i++) {
            const uint8_t *s = p + 2 + i * YG_TRACE_SAMPLE_SIZE;
            yg_jw_raw(w, "%s[%d,%u,%u,%u,%u]", i ? "," : "", (int)(int8_t)s[0], s[1], s[2] * 10u, s[3], s[4]);
        }
        yg_jw_raw(w, "]");
        break;
    }
    case YG_REC_TEMP:
        yg_jw_raw(w, "\"k\":\"temp\",\"c\":");
        yg_jw_tenths(w, (int16_t)yg_get_u16(p));
        break;
    case YG_REC_RATING:
        yg_jw_raw(w, "\"k\":\"rating\",\"rated_trip\":%u,\"rating\":\"%s\"", (unsigned)yg_get_u32(p), yg_rating_name(p[4]));
        break;
    case YG_REC_TIME_ANCHOR:
        yg_jw_raw(w, "\"k\":\"time_anchor\",\"epoch\":%u", (unsigned)yg_get_u32(p));
        break;
    case YG_REC_ENV:
        if (!p[18]) {
            yg_jw_raw(w, "\"k\":\"env\",\"ble\":%u,\"wifi\":null,\"wifi_best\":null,\"chan\":null", yg_get_u16(p));
            break;
        }
        yg_jw_raw(w, "\"k\":\"env\",\"ble\":%u,\"wifi\":%u,\"wifi_best\":%d,\"chan\":[", yg_get_u16(p), yg_get_u16(p + 2), (int)(int8_t)p[4]);
        for (int i = 0; i < 13; i++) {
            yg_jw_raw(w, "%s%u", i ? "," : "", p[5 + i]);
        }
        yg_jw_raw(w, "]");
        break;
    case YG_REC_CAP_START:
        yg_jw_raw(w, "\"k\":\"cap_start\",\"cap\":%u,\"cause\":\"%s\",\"pre\":%u", (unsigned)yg_get_u32(p), cap_cause_name(p[4]), yg_get_u16(p + 5));
        break;
    case YG_REC_CAP_IMU:
        yg_jw_raw(w, "\"k\":\"cap_imu\",\"cap\":%u,\"first\":%u,\"n\":%u,\"b64\":\"", (unsigned)yg_get_u32(p), (unsigned)yg_get_u32(p + 4), p[8]);
        json_base64(w, p + 9, (size_t)p[8] * YG_CAP_IMU_SAMPLE);
        yg_jw_raw(w, "\"");
        break;
    case YG_REC_CAP_ADV:
        yg_jw_raw(w, "\"k\":\"cap_adv\",\"cap\":%u,\"n\":%u,\"b64\":\"", (unsigned)yg_get_u32(p), p[4]);
        json_base64(w, p + 5, (size_t)p[4] * YG_CAP_ADV_ENTRY);
        yg_jw_raw(w, "\"");
        break;
    case YG_REC_CAP_END:
        yg_jw_raw(w, "\"k\":\"cap_end\",\"cap\":%u,\"samples\":%u,\"adverts\":%u", (unsigned)yg_get_u32(p), (unsigned)yg_get_u32(p + 4), (unsigned)yg_get_u32(p + 8));
        break;
    default:
        yg_jw_raw(w, "\"k\":\"unknown\",\"type\":%u", r->type);
        break;
    }
}

int yg_record_to_json(const yg_record_t *r, char *buf, size_t len) {
    yg_jw_t w;
    yg_jw_init(&w, buf, len);
    yg_jw_raw(&w, "{");
    json_body(&w, r);
    yg_jw_raw(&w, ",\"trip\":%u,\"t\":%u}", (unsigned)r->trip, (unsigned)r->uptime_ms);
    return yg_jw_finish(&w);
}
