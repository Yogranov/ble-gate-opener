#include "yg_motion.h"

#include <math.h>
#include <string.h>

#define STILL_SAMPLES        50        /* 1 s at 50 Hz */
#define AVERAGE_SAMPLES      100       /* 2 s at 50 Hz */
#define STILL_GYRO_SPREAD    3.0f      /* degrees per second, peak to peak */
#define STILL_ACCEL_SPREAD   0.02f     /* g, peak to peak */
#define CALIBRATE_DEADLINE   10000u    /* ms: calibrate anyway after this */
#define REST_WINDOWS         30        /* Still seconds in a row before trusting the sensor again. */
#define TILT_RATE            8.0f      /* degrees per second */
#define TILT_HOLD_MS         1000u
#define HORIZONTAL_TAU_S     0.08f     /* About 2 Hz. */
#define GRAVITY_TAU_S        10.0f
#define GRAVITY_TAU_ABOVE_S  30.0f     /* While above the threshold: braking must not look like a tilt after. */
#define GRAVITY_ALPHA_TILT   0.1f
#define BIAS_TAU_S           10.0f
#define DRIFT_RATE           1.0f      /* degrees per second: slower is sensor drift, not rotation. */
#define NEAR_MISS_FRACTION   0.5f
#define BUCKET_MS            100u
#define WINDOW_S             1.0f      /* STILL_SAMPLES at 50 Hz. */
#define DEG_TO_RAD           0.017453293f

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

/* Adds a sample to the 1 s window. When the window fills, returns true and gives
 * its mean, and whether it was quiet. */
static bool window_add(yg_motion_t *m, const float v[6], float mean[6], bool *still) {
    for (int i = 0; i < 6; i++) {
        m->still_sum[i] += v[i];
        m->still_min[i] = v[i] < m->still_min[i] ? v[i] : m->still_min[i];
        m->still_max[i] = v[i] > m->still_max[i] ? v[i] : m->still_max[i];
    }
    m->still_n++;
    if (m->still_n < STILL_SAMPLES) {
        return false;
    }
    *still = true;
    for (int i = 0; i < 6; i++) {
        float spread = m->still_max[i] - m->still_min[i];
        *still = *still && spread < (i < 3 ? STILL_ACCEL_SPREAD : STILL_GYRO_SPREAD);
        mean[i] = m->still_sum[i] / (float)m->still_n;
    }
    reset_still_window(m);
    return true;
}

/* Finds gravity and gyro bias: from 1 s of stillness, or from a 2 s average if
 * stillness never comes before the deadline. */
static void calibrate(yg_motion_t *m, uint32_t now, const float v[6]) {
    float mean[6];
    bool still;
    if (window_add(m, v, mean, &still) && still) {
        set_calibration(m, mean);
        return;
    }
    for (int i = 0; i < 6; i++) {
        m->avg_sum[i] += v[i];
    }
    m->avg_n++;
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

/* The gyro's zero moves as the car warms up. Over a whole second the engine's shake
 * averages out, and what little is left is that drift. */
static void follow_drift(yg_motion_t *m, const float mean[6]) {
    float rot[3];
    for (int i = 0; i < 3; i++) {
        rot[i] = mean[i + 3] - m->bias[i];
    }
    if (length(rot) < DRIFT_RATE) {
        for (int i = 0; i < 3; i++) {
            m->bias[i] += rot[i] * WINDOW_S / BIAS_TAU_S;
        }
    }
}

/* A unit at rest for a long time tells the truth about gravity and gyro bias, whatever
 * went wrong before, such as calibrating in a bend. Real rotation, however slow, shows
 * as gravity moving between windows, and so does not count as rest. */
static void watch_for_rest(yg_motion_t *m, const float v[6]) {
    float mean[6];
    bool still;
    if (!window_add(m, v, mean, &still)) {
        return;
    }
    follow_drift(m, mean);
    if (!still) {
        m->rest_windows = 0;
        return;
    }
    bool same = m->rest_windows > 0;
    for (int i = 0; i < 3 && same; i++) {
        same = fabsf(mean[i] - m->rest_gravity[i]) < STILL_ACCEL_SPREAD;
    }
    if (!same) {
        memcpy(m->rest_gravity, mean, sizeof(m->rest_gravity));
        m->rest_windows = 0;
    }
    m->rest_windows++;
    if (m->rest_windows >= REST_WINDOWS) {
        set_calibration(m, mean);
        m->rest_windows = 0;
    }
}

/* The vertical is fixed in the world, so in the sensor's frame it turns the other way. */
static void turn_gravity(float g[3], const float rot[3], float dt) {
    const float w[3] = { rot[0] * DEG_TO_RAD * dt, rot[1] * DEG_TO_RAD * dt, rot[2] * DEG_TO_RAD * dt };
    const float c[3] = { w[1] * g[2] - w[2] * g[1], w[2] * g[0] - w[0] * g[2], w[0] * g[1] - w[1] * g[0] };
    for (int i = 0; i < 3; i++) {
        g[i] -= c[i];
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
    const float v[6] = { in->ax, in->ay, in->az, in->gx, in->gy, in->gz };

    if (!m->started) {
        m->started = true;
        m->first_ms = now;
        m->last_ms = now;
        m->bucket_start = now;
    }
    if (!m->calibrated) {
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

    float rot[3];
    for (int i = 0; i < 3; i++) {
        rot[i] = gyro[i] - m->bias[i];
    }
    float rot_rate = length(rot);
    if (rot_rate >= DRIFT_RATE) {
        turn_gravity(m->gravity, rot, dt);
    }

    float glen = length(m->gravity);
    float down[3] = { m->gravity[0] / glen, m->gravity[1] / glen, m->gravity[2] / glen };

    /* Split rotation into yaw, around the vertical, and tilt, around anything else. */
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
    float threshold = (float)s->motion_accel_mg;
    bool above = !tilting && horizontal_mg >= threshold;

    /* Gravity follows slowly, or quickly while the unit itself is being tilted. */
    float g_alpha = tilting ? GRAVITY_ALPHA_TILT : dt / (above ? GRAVITY_TAU_ABOVE_S : GRAVITY_TAU_S);
    for (int i = 0; i < 3; i++) {
        m->gravity[i] += g_alpha * (accel[i] - m->gravity[i]);
    }
    if (tilting) {
        for (int i = 0; i < 3; i++) {
            m->horizontal[i] = 0;
        }
        horizontal_mg = 0;
    }

    float yaw_sum = yaw_window_sum(m, s, now, yaw_rate * dt);

    if (above && !m->above) {
        m->above_since = now;
    }
    m->above = above;

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

    watch_for_rest(m, v);
}

void yg_motion_take_peaks(yg_motion_t *m, uint16_t *accel_mg, uint16_t *yaw_dps) {
    *accel_mg = (uint16_t)(m->peak_accel_mg + 0.5f);
    *yaw_dps = (uint16_t)(m->peak_yaw_dps + 0.5f);
    m->peak_accel_mg = 0;
    m->peak_yaw_dps = 0;
}
