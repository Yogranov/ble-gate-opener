#include "yg_motion.h"

#include <math.h>
#include <string.h>

#define WINDOW_SAMPLES       50        /* 1 s at 50 Hz */
#define WINDOW_S             1.0f
#define SAME_ACCEL           0.006f    /* g: means this close show that nothing has moved. */
#define SAME_GYRO            0.3f      /* degrees per second, the same. */
#define MIN_GRAVITY          0.7f      /* g: anything else is not a sensor at rest. */
#define MAX_GRAVITY          1.3f
#define CALIBRATE_LAST       20000u    /* ms: calibrate after this, whatever the means say. */
#define REST_WINDOWS         30        /* Seconds of unchanged means before trusting the sensor again. */
#define TILT_RATE            8.0f      /* degrees per second */
#define TILT_HOLD_MS         1000u
#define TILT_CATCH_UP_MS     100u      /* Hold on this much longer while gravity is still off. */
#define GAP_HOLD_EVERY_MS    5000u     /* A bus losing most reads must not hold for ever. */
#define HORIZONTAL_TAU_S     0.08f     /* About 2 Hz. */
#define GRAVITY_TAU_S        3.0f      /* A fading gyro zero error under DRIFT_RATE stays under the threshold. */
#define GRAVITY_TAU_ABOVE_S  30.0f     /* While above the threshold: braking must not look like a tilt after. */
#define GRAVITY_TAU_TILT_S   1.0f      /* Soon after a move, though a door's shove must not count for much. */
#define BIAS_TAU_S           10.0f
#define DRIFT_RATE           1.0f      /* degrees per second: a whole second slower than this is drift. */
#define NEAR_MISS_FRACTION   0.5f
#define BUCKET_MS            100u
#define DEG_TO_RAD           0.017453293f

static float dot(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static float length(const float v[3]) {
    return sqrtf(dot(v, v));
}

static void set_calibration(yg_motion_t *m, const float mean[6]) {
    for (int i = 0; i < 3; i++) {
        m->gravity[i] = mean[i];
        m->bias[i] = mean[i + 3];
        m->horizontal[i] = 0;
    }
    memcpy(m->prev_mean, mean, sizeof(m->prev_mean));
    m->calibrated = true;
}

static bool accel_agrees(const float a[6], const float b[6]) {
    for (int i = 0; i < 3; i++) {
        if (fabsf(a[i] - b[i]) >= SAME_ACCEL) {
            return false;
        }
    }
    return true;
}

static bool gyro_agrees(const float a[6], const float b[6]) {
    for (int i = 3; i < 6; i++) {
        if (fabsf(a[i] - b[i]) >= SAME_GYRO) {
            return false;
        }
    }
    return true;
}

static bool gravity_plausible(const float mean[6]) {
    float g = length(mean);
    return g >= MIN_GRAVITY && g <= MAX_GRAVITY;
}

/* Adds a sample to the 1 s window. When the window fills, returns true and gives its
 * mean, in which engine shake has cancelled out. */
static bool window_add(yg_motion_t *m, const float v[6], float mean[6]) {
    for (int i = 0; i < 6; i++) {
        m->window_sum[i] += v[i];
    }
    m->window_n++;
    if (m->window_n < WINDOW_SAMPLES) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        mean[i] = m->window_sum[i] / (float)m->window_n;
        m->window_sum[i] = 0;
    }
    m->window_n = 0;
    return true;
}

/* Finds gravity and gyro bias, once two seconds in a row agree. That leaves little room
 * for a slow lean to pass as bias, and a passenger getting in only delays it. A car
 * already rolling may never agree, so after a while it takes what there is. */
static void calibrate(yg_motion_t *m, uint32_t now, const float v[6]) {
    float mean[6];
    if (!window_add(m, v, mean)) {
        return;
    }
    bool steady = m->has_prev_mean && accel_agrees(mean, m->prev_mean) && gyro_agrees(mean, m->prev_mean);
    bool time_up = now - m->first_ms >= CALIBRATE_LAST;
    if ((steady || time_up) && gravity_plausible(mean)) {
        set_calibration(m, mean);
    }
    memcpy(m->prev_mean, mean, sizeof(m->prev_mean));
    m->has_prev_mean = true;
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
 * went wrong before, such as calibrating in a bend. Whole-second means are steady with
 * the engine shaking too. Gravity must stay where the rest began, as a slow turn moves it
 * a little at a time, while the gyro's zero may drift, so it is only compared with the
 * second before. A steady rate around the vertical, as in a long bend, looks like rest,
 * and is taken for bias until the next one. */
static void watch_for_rest(yg_motion_t *m, const float mean[6]) {
    if (m->rest_windows == 0 || !accel_agrees(mean, m->rest_mean) || !gyro_agrees(mean, m->prev_mean)) {
        memcpy(m->rest_mean, mean, sizeof(m->rest_mean));
        m->rest_windows = 0;
    }
    m->rest_windows++;
    if (m->rest_windows >= REST_WINDOWS && gravity_plausible(mean)) {
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
    if (dt > 0.1f && (int32_t)(now - m->next_gap_hold) >= 0) {
        /* The unit may have been moved while no samples came. */
        m->tilt_until = now + TILT_HOLD_MS;
        m->next_gap_hold = now + GAP_HOLD_EVERY_MS;
    }
    if (dt <= 0.0f || dt > 0.1f) {
        dt = 0.02f;
    }

    /* Gravity turns with the gyro, so that a lean leaves no error behind. */
    float rot[3];
    for (int i = 0; i < 3; i++) {
        rot[i] = gyro[i] - m->bias[i];
    }
    turn_gravity(m->gravity, rot, dt);

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

    /* Horizontal acceleration, low-passed as a vector so that shake cancels out. */
    float along = dot(accel, down);
    float alpha = dt / (HORIZONTAL_TAU_S + dt);
    for (int i = 0; i < 3; i++) {
        float h = accel[i] - along * down[i];
        m->horizontal[i] += alpha * (h - m->horizontal[i]);
    }
    float horizontal_mg = length(m->horizontal) * 1000.0f;
    float threshold = (float)s->motion_accel_mg;

    /* After a move that the gyro could not follow, gravity is still off when the hold ends. */
    bool tilting = (int32_t)(m->tilt_until - now) > 0;
    if (tilting && horizontal_mg >= threshold && (int32_t)(m->tilt_until - now) < (int32_t)TILT_CATCH_UP_MS) {
        m->tilt_until = now + TILT_CATCH_UP_MS;
    }
    if (tilting) {
        horizontal_mg = 0;
    }
    bool above = horizontal_mg >= threshold;

    float tau = tilting ? GRAVITY_TAU_TILT_S : above ? GRAVITY_TAU_ABOVE_S : GRAVITY_TAU_S;
    for (int i = 0; i < 3; i++) {
        m->gravity[i] += dt / tau * (accel[i] - m->gravity[i]);
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

    float mean[6];
    if (window_add(m, v, mean)) {
        follow_drift(m, mean);
        watch_for_rest(m, mean);
        memcpy(m->prev_mean, mean, sizeof(m->prev_mean));
    }
}

void yg_motion_take_peaks(yg_motion_t *m, uint16_t *accel_mg, uint16_t *yaw_dps) {
    *accel_mg = (uint16_t)(m->peak_accel_mg + 0.5f);
    *yaw_dps = (uint16_t)(m->peak_yaw_dps + 0.5f);
    m->peak_accel_mg = 0;
    m->peak_yaw_dps = 0;
}
