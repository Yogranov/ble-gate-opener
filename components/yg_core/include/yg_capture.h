/* Debug captures: raw motion samples and every advert, from 10 s before an event
 * to 30 s after it. See the design spec, section 9.4. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "yg_motion.h"
#include "yg_record.h"

#define YG_CAP_IMU_RING 512    /* Just over 10 s at 50 Hz. */
#define YG_CAP_ADV_RING 128
#define YG_CAP_PRE_SAMPLES 500
#define YG_CAP_POST_MS  30000u
#define YG_CAP_PRE_MS   10000u

/* Scale of the raw samples in capture records. */
#define YG_CAP_ACCEL_LSB_PER_G   16384.0f
#define YG_CAP_GYRO_LSB_PER_DPS  131.0f

typedef enum
{
    YG_CAP_IDLE,
    YG_CAP_START_PENDING,
    YG_CAP_IMU,
    YG_CAP_ADV,
    YG_CAP_END_PENDING,
} yg_cap_phase_t;

typedef struct
{
    int16_t imu[YG_CAP_IMU_RING][6];
    uint32_t imu_count;
    uint32_t adv_time[YG_CAP_ADV_RING];
    int8_t adv_rssi[YG_CAP_ADV_RING];
    uint32_t adv_count;

    yg_cap_phase_t phase;
    uint32_t id;
    uint8_t cause;
    uint32_t started;
    uint32_t emit_imu;
    uint32_t first_imu;
    uint32_t emit_adv;
    uint32_t adverts_written;
    uint32_t sequence;
} yg_capture_t;

void yg_capture_init(yg_capture_t *c);
void yg_capture_on_imu(yg_capture_t *c, const yg_imu_sample_t *s);
void yg_capture_on_advert(yg_capture_t *c, uint32_t now, int8_t rssi);

/* Starts a capture unless one is running. Returns true if it started. */
bool yg_capture_trigger(yg_capture_t *c, uint32_t now, uint32_t trip, uint8_t cause);
bool yg_capture_active(const yg_capture_t *c);

/* Returns true when `out` holds the next record to store. */
bool yg_capture_next(yg_capture_t *c, uint32_t now, uint32_t trip, yg_record_t *out);
