/* Tells driving apart from door slams, passengers and engine shake, using one
 * IMU in any orientation. See the design spec, section 6. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "yg_settings.h"

#define YG_MOTION_YAW_BUCKETS 100   /* 100 ms each, enough for the longest yaw window. */

typedef struct
{
    float ax, ay, az;   /* g */
    float gx, gy, gz;   /* degrees per second */
} yg_imu_sample_t;

typedef enum
{
    YG_MOTION_NONE,
    YG_MOTION_YAW,
    YG_MOTION_ACCEL,
} yg_motion_cause_t;

typedef struct
{
    yg_motion_cause_t cause;
    bool calibrated;
    bool tilting;
    bool near_miss;        /* Something happened, at least half way to a detection. */
    float horizontal_mg;
    float yaw_sum_deg;
} yg_motion_out_t;

typedef enum
{
    YG_ZERO_NONE,
    YG_ZERO_STEADY,     /* Two seconds in a row agreed at the start. */
    YG_ZERO_TIME_UP,    /* Nothing agreed in time and none was known, so it took what there was. */
    YG_ZERO_KNOWN,      /* Nothing agreed in time, so it took the one from an earlier trip. */
    YG_ZERO_REST,       /* A long rest showed that it had moved. */
} yg_zero_source_t;

typedef struct
{
    yg_zero_source_t source;
    bool keep;          /* Worth saving for the next start. */
    float zero[3];      /* Degrees per second. */
} yg_zero_event_t;

typedef struct
{
    bool started;
    bool calibrated;
    uint32_t first_ms;
    uint32_t last_ms;

    uint16_t window_n;
    float window_sum[6];
    bool has_prev_mean;

    float gravity[3];
    float bias[3];
    float horizontal[3];

    float prev_mean[6];
    uint32_t next_gap_hold;
    uint16_t rest_windows;
    float rest_mean[6];

    bool above;
    uint32_t above_since;
    uint32_t tilt_until;

    float yaw_bucket[YG_MOTION_YAW_BUCKETS];
    uint16_t yaw_head;
    uint32_t bucket_start;

    float peak_accel_mg;
    float peak_yaw_dps;

    bool has_known_zero;
    float known_zero[3];
    yg_zero_source_t zero_source;
    bool zero_keep;
    float start_sum[6];
    uint16_t start_windows;
} yg_motion_t;

void yg_motion_init(yg_motion_t *m);

/* The gyro zero saved by an earlier trip. Call after init. A car that never holds still
 * for calibration has no way to measure one, and guessing wrong looks like driving.
 * An impossible value is ignored. */
void yg_motion_set_known_zero(yg_motion_t *m, const float zero[3]);

/* True once for each zero worth telling anyone about: the one this trip started with,
 * and any that a long rest shows has moved. */
bool yg_motion_take_zero_event(yg_motion_t *m, yg_zero_event_t *out);

void yg_motion_update(yg_motion_t *m, const yg_settings_t *s, uint32_t now, const yg_imu_sample_t *in, yg_motion_out_t *out);

/* Returns the strongest values since the last call, then clears them. */
void yg_motion_take_peaks(yg_motion_t *m, uint16_t *accel_mg, uint16_t *yaw_dps);
