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

typedef struct
{
    bool started;
    bool calibrated;
    uint32_t first_ms;
    uint32_t last_ms;

    uint16_t still_n;
    float still_sum[6];
    float still_min[6];
    float still_max[6];
    uint16_t avg_n;
    float avg_sum[6];
    float avg_last[6];
    bool avg_valid;

    float gravity[3];
    float bias[3];
    float horizontal[3];

    uint16_t rest_windows;
    float rest_gravity[3];

    bool above;
    uint32_t above_since;
    uint32_t tilt_until;

    float yaw_bucket[YG_MOTION_YAW_BUCKETS];
    uint16_t yaw_head;
    uint32_t bucket_start;

    float peak_accel_mg;
    float peak_yaw_dps;
} yg_motion_t;

void yg_motion_init(yg_motion_t *m);
void yg_motion_update(yg_motion_t *m, const yg_settings_t *s, uint32_t now, const yg_imu_sample_t *in, yg_motion_out_t *out);

/* Returns the strongest values since the last call, then clears them. */
void yg_motion_take_peaks(yg_motion_t *m, uint16_t *accel_mg, uint16_t *yaw_dps);
