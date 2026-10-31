#include "yg_motion.h"

#include <math.h>
#include <string.h>

#define STILL_SAMPLES        50        /* 1 s at 50 Hz */
#define AVERAGE_SAMPLES      100       /* 2 s at 50 Hz */
#define STILL_GYRO_SPREAD    3.0f      /* degrees per second, peak to peak */
#define STILL_ACCEL_SPREAD   0.02f     /* g, peak to peak */
#define CALIBRATE_DEADLINE   10000u    /* ms: calibrate anyway after this */
#define TILT_RATE            8.0f      /* degrees per second */
#define TILT_HOLD_MS         1000u
#define ACCEL_RELEASE        0.7f      /* Fraction of the threshold that ends a detection. */
#define HORIZONTAL_TAU_S     0.08f     /* About 2 Hz. */
#define GRAVITY_TAU_S        10.0f
#define GRAVITY_ALPHA_TILT   0.1f
#define BIAS_TAU_S           10.0f
#define BIAS_QUIET_GYRO      1.5f      /* degrees per second */
#define BIAS_QUIET_MG        15.0f
#define NEAR_MISS_FRACTION   0.5f
#define BUCKET_MS            100u

static float dot(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static float length(const float v[3]) {
    return sqrtf(dot(v, v));
}

static void reset_still_window(yg_motion_t *m) {
    m->still_n = 0;
    for (int i = 0; i < 6; i++) {
        m->still_sum[i] = 0;
        m->still_min[i] = 1e9f;
        m->still_max[i] = -1e9f;
    }
}

static void set_calibration(yg_motion_t *m, const float mean[6]) {
    for (int i = 0; i < 3; i++) {
        m->gravity[i] = mean[i];
        m->bias[i] = mean[i + 3];
        m->horizontal[i] = 0;
    }
    m->calibrated = true;
}

/* Finds gravity and gyro bias: from 1 s of stillness, or from a 2 s average if
 * stillness never comes before the deadline. */
static void calibrate(yg_motion_t *m, uint32_t now, const float v[6]) {
    for (int i = 0; i < 6; i++) {
        m->still_sum[i] += v[i];
        m->still_min[i] = v[i] < m->still_min[i] ? v[i] : m->still_min[i];
        m->still_max[i] = v[i] > m->still_max[i] ? v[i] : m->still_max[i];
        m->avg_sum[i] += v[i];
    }
    m->still_n++;
    m->avg_n++;

    if (m->still_n >= STILL_SAMPLES) {
        bool still = true;
        float mean[6];
        for (int i = 0; i < 6; i++) {
            float spread = m->still_max[i] - m->still_min[i];
            still = still && spread < (i < 3 ? STILL_ACCEL_SPREAD : STILL_GYRO_SPREAD);
            mean[i] = m->still_sum[i] / (float)m->still_n;
        }
        reset_still_window(m);
        if (still) {
            set_calibration(m, mean);
            return;
        }
    }
    if (m->avg_n >= AVERAGE_SAMPLES) {
        for (int i = 0; i < 6; i++) {
            m->avg_last[i] = m->avg_sum[i] / (float)m->avg_n;
            m->avg_sum[i] = 0;
        }
        m->avg_n = 0;
        m->avg_valid = true;
    }
    if (m->avg_valid && now - m->first_ms >= CALIBRATE_DEADLINE) {
        set_calibration(m, m->avg_last);
    }
}

static float yaw_window_sum(yg_motion_t *m, const yg_settings_t *s, uint32_t now, float yaw_step) {
    while (now - m->bucket_start >= BUCKET_MS) {
        m->yaw_head = (uint16_t)((m->yaw_head + 1) % YG_MOTION_YAW_BUCKETS);
        m->yaw_bucket[m->yaw_head] = 0;
        m->bucket_start += BUCKET_MS;
    }
    m->yaw_bucket[m->yaw_head] += yaw_step;

    uint32_t buckets = s->motion_yaw_window_ms / BUCKET_MS;
    if (buckets > YG_MOTION_YAW_BUCKETS) {
        buckets = YG_MOTION_YAW_BUCKETS;
    }
    float sum = 0;
    for (uint32_t i = 0; i < buckets; i++) {
        sum += m->yaw_bucket[(m->yaw_head + YG_MOTION_YAW_BUCKETS - i) % YG_MOTION_YAW_BUCKETS];
    }
    return sum;
}

void yg_motion_init(yg_motion_t *m) {
    memset(m, 0, sizeof(*m));
    reset_still_window(m);
}

void yg_motion_update(yg_motion_t *m, const yg_settings_t *s, uint32_t now, const yg_imu_sample_t *in, yg_motion_out_t *out) {
    memset(out, 0, sizeof(*out));
    const float accel[3] = { in->ax, in->ay, in->az };
    const float gyro[3] = { in->gx, in->gy, in->gz };

    if (!m->started) {
        m->started = true;
        m->first_ms = now;
        m->last_ms = now;
        m->bucket_start = now;
    }
    if (!m->calibrated) {
        const float v[6] = { in->ax, in->ay, in->az, in->gx, in->gy, in->gz };
        calibrate(m, now, v);
        m->last_ms = now;
        m->bucket_start = now;
        out->calibrated = m->calibrated;
        return;
    }

    float dt = (float)(now - m->last_ms) / 1000.0f;
    m->last_ms = now;
    if (dt <= 0.0f || dt > 0.1f) {
        dt = 0.02f;
    }

    float glen = length(m->gravity);
    float down[3] = { m->gravity[0] / glen, m->gravity[1] / glen, m->gravity[2] / glen };

    /* Split rotation into yaw, around the vertical, and tilt, around anything else. */
    float rot[3];
    for (int i = 0; i < 3; i++) {
        rot[i] = gyro[i] - m->bias[i];
    }
    float yaw_rate = dot(rot, down);
    float tilt_vec[3];
    for (int i = 0; i < 3; i++) {
        tilt_vec[i] = rot[i] - yaw_rate * down[i];
    }
    float tilt_rate = length(tilt_vec);
    if (tilt_rate > TILT_RATE) {
        m->tilt_until = now + TILT_HOLD_MS;
    }
    bool tilting = (int32_t)(m->tilt_until - now) > 0;

    /* Horizontal acceleration, low-passed as a vector so that shake cancels out. */
    float along = dot(accel, down);
    float alpha = dt / (HORIZONTAL_TAU_S + dt);
    for (int i = 0; i < 3; i++) {
        float h = accel[i] - along * down[i];
        m->horizontal[i] += alpha * (h - m->horizontal[i]);
    }
    float horizontal_mg = length(m->horizontal) * 1000.0f;

    /* Gravity follows slowly, or quickly while the unit itself is being tilted. */
    float g_alpha = tilting ? GRAVITY_ALPHA_TILT : dt / GRAVITY_TAU_S;
    for (int i = 0; i < 3; i++) {
        m->gravity[i] += g_alpha * (accel[i] - m->gravity[i]);
    }
    if (tilting) {
        for (int i = 0; i < 3; i++) {
            m->horizontal[i] = 0;
        }
        horizontal_mg = 0;
        m->above = false;
    }

    float rot_rate = length(rot);
    if (!tilting && rot_rate < BIAS_QUIET_GYRO && horizontal_mg < BIAS_QUIET_MG) {
        for (int i = 0; i < 3; i++) {
            m->bias[i] += dt / BIAS_TAU_S * rot[i];
        }
    }

    float yaw_sum = yaw_window_sum(m, s, now, yaw_rate * dt);

    float threshold = (float)s->motion_accel_mg;
    if (!tilting && horizontal_mg >= threshold) {
        if (!m->above) {
            m->above = true;
            m->above_since = now;
        }
    } else if (horizontal_mg < threshold * ACCEL_RELEASE) {
        m->above = false;
    }

    out->calibrated = true;
    out->tilting = tilting;
    out->horizontal_mg = horizontal_mg;
    out->yaw_sum_deg = yaw_sum;
    if (fabsf(yaw_sum) >= (float)s->motion_yaw_deg) {
        out->cause = YG_MOTION_YAW;
    } else if (m->above && now - m->above_since >= s->motion_accel_ms) {
        out->cause = YG_MOTION_ACCEL;
    }
    out->near_miss = tilting || horizontal_mg >= threshold * NEAR_MISS_FRACTION
                     || fabsf(yaw_sum) >= (float)s->motion_yaw_deg * NEAR_MISS_FRACTION;

    if (horizontal_mg > m->peak_accel_mg) {
        m->peak_accel_mg = horizontal_mg;
    }
    if (fabsf(yaw_rate) > m->peak_yaw_dps) {
        m->peak_yaw_dps = fabsf(yaw_rate);
    }
}

void yg_motion_take_peaks(yg_motion_t *m, uint16_t *accel_mg, uint16_t *yaw_dps) {
    *accel_mg = (uint16_t)(m->peak_accel_mg + 0.5f);
    *yaw_dps = (uint16_t)(m->peak_yaw_dps + 0.5f);
    m->peak_accel_mg = 0;
    m->peak_yaw_dps = 0;
}
