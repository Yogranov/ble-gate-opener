#include "yg_capture.h"

#include <string.h>

#define ADV_FLUSH_AT 8

void yg_capture_init(yg_capture_t *c) {
    memset(c, 0, sizeof(*c));
}

static int16_t to_raw(float value, float scale) {
    float raw = value * scale;
    if (raw > 32767.0f) return 32767;
    if (raw < -32768.0f) return -32768;
    return (int16_t)(raw + (raw >= 0 ? 0.5f : -0.5f));
}

void yg_capture_on_imu(yg_capture_t *c, const yg_imu_sample_t *s) {
    int16_t *slot = c->imu[c->imu_count % YG_CAP_IMU_RING];
    slot[0] = to_raw(s->ax, YG_CAP_ACCEL_LSB_PER_G);
    slot[1] = to_raw(s->ay, YG_CAP_ACCEL_LSB_PER_G);
    slot[2] = to_raw(s->az, YG_CAP_ACCEL_LSB_PER_G);
    slot[3] = to_raw(s->gx, YG_CAP_GYRO_LSB_PER_DPS);
    slot[4] = to_raw(s->gy, YG_CAP_GYRO_LSB_PER_DPS);
    slot[5] = to_raw(s->gz, YG_CAP_GYRO_LSB_PER_DPS);
    c->imu_count++;
}

void yg_capture_on_advert(yg_capture_t *c, uint32_t now, int8_t rssi) {
    uint32_t slot = c->adv_count % YG_CAP_ADV_RING;
    c->adv_time[slot] = now;
    c->adv_rssi[slot] = rssi;
    c->adv_count++;
}

bool yg_capture_trigger(yg_capture_t *c, uint32_t now, uint32_t trip, uint8_t cause) {
    if (c->phase != YG_CAP_IDLE) {
        return false;
    }
    c->id = trip * 1000u + (c->sequence++ % 1000u);
    c->cause = cause;
    c->started = now;
    c->first_imu = c->imu_count > YG_CAP_PRE_SAMPLES ? c->imu_count - YG_CAP_PRE_SAMPLES : 0;
    c->pre_samples = (uint16_t)(c->imu_count - c->first_imu);
    c->emit_imu = c->first_imu;
    c->emit_adv = c->adv_count > YG_CAP_ADV_RING ? c->adv_count - YG_CAP_ADV_RING : 0;
    c->adverts_written = 0;
    c->phase = YG_CAP_START_PENDING;
    return true;
}

bool yg_capture_active(const yg_capture_t *c) {
    return c->phase != YG_CAP_IDLE;
}

static bool next_imu(yg_capture_t *c, uint32_t now, uint32_t trip, yg_record_t *out, bool done) {
    if (c->imu_count - c->emit_imu > YG_CAP_IMU_RING) {
        c->emit_imu = c->imu_count - YG_CAP_IMU_RING;
    }
    uint32_t available = c->imu_count - c->emit_imu;
    if (available == 0 || (available < YG_CAP_IMU_PER_RECORD && !done)) {
        return false;
    }
    uint8_t n = available < YG_CAP_IMU_PER_RECORD ? (uint8_t)available : YG_CAP_IMU_PER_RECORD;
    int16_t raw[6 * YG_CAP_IMU_PER_RECORD];
    for (uint8_t i = 0; i < n; i++) {
        memcpy(&raw[i * 6], c->imu[(c->emit_imu + i) % YG_CAP_IMU_RING], sizeof(int16_t) * 6);
    }
    yg_rec_cap_imu(out, trip, now, c->id, c->emit_imu - c->first_imu, n, raw);
    c->emit_imu += n;
    return true;
}

static bool next_adv(yg_capture_t *c, uint32_t now, uint32_t trip, yg_record_t *out) {
    uint8_t entries[YG_CAP_ADV_ENTRY * YG_CAP_ADV_PER_RECORD];
    uint8_t n = 0;
    uint32_t window_start = c->started - YG_CAP_PRE_MS;
    while (c->emit_adv < c->adv_count && n < YG_CAP_ADV_PER_RECORD) {
        uint32_t slot = c->emit_adv % YG_CAP_ADV_RING;
        c->emit_adv++;
        uint32_t t = c->adv_time[slot];
        if ((int32_t)(t - window_start) < 0) {
            continue;
        }
        uint8_t *e = &entries[n * YG_CAP_ADV_ENTRY];
        e[0] = (uint8_t)t;
        e[1] = (uint8_t)(t >> 8);
        e[2] = (uint8_t)(t >> 16);
        e[3] = (uint8_t)(t >> 24);
        e[4] = (uint8_t)c->adv_rssi[slot];
        n++;
    }
    if (n == 0) {
        return false;
    }
    yg_rec_cap_adv(out, trip, now, c->id, n, entries);
    c->adverts_written += n;
    return true;
}

bool yg_capture_next(yg_capture_t *c, uint32_t now, uint32_t trip, yg_record_t *out) {
    bool done = now - c->started >= YG_CAP_POST_MS;
    switch (c->phase) {
    case YG_CAP_START_PENDING:
        yg_rec_cap_start(out, trip, c->started, c->id, c->cause, c->pre_samples);
        c->phase = YG_CAP_IMU;
        return true;
    case YG_CAP_IMU:
        /* Power can go at any moment, so adverts do not wait for the end. */
        if (c->adv_count - c->emit_adv >= ADV_FLUSH_AT && next_adv(c, now, trip, out)) {
            return true;
        }
        if (next_imu(c, now, trip, out, done)) {
            return true;
        }
        if (!done) {
            return false;
        }
        c->phase = YG_CAP_ADV;
        /* fall through */
    case YG_CAP_ADV:
        if (next_adv(c, now, trip, out)) {
            return true;
        }
        c->phase = YG_CAP_END_PENDING;
        /* fall through */
    case YG_CAP_END_PENDING:
        yg_rec_cap_end(out, trip, now, c->id, c->emit_imu - c->first_imu, c->adverts_written);
        c->phase = YG_CAP_IDLE;
        return true;
    default:
        return false;
    }
}
