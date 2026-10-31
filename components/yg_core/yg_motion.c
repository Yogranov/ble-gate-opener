#include "yg_motion.h"

#include <math.h>
#include <string.h>

#define STILL_SAMPLES        50        /* 1 s at 50 Hz */
#define AVERAGE_SAMPLES      100       /* 2 s at 50 Hz */
#define WINDOW_S             1.0f      /* STILL_SAMPLES at 50 Hz. */
#define STILL_GYRO_SPREAD    3.0f      /* degrees per second, peak to peak */
#define STILL_ACCEL_SPREAD   0.02f     /* g, peak to peak */
#define SAME_ACCEL           0.006f    /* g: means this close show that nothing has moved. */
#define SAME_GYRO            0.3f      /* degrees per second, the same. */
#define MIN_GRAVITY          0.7f      /* g: anything else is not a sensor at rest. */
#define MAX_GRAVITY          1.3f
#define CALIBRATE_DEADLINE   10000u    /* ms: calibrate from averages after this, if they agree. */
#define CALIBRATE_LAST       20000u    /* ms: calibrate after this, whatever they say. */
#define REST_WINDOWS         30        /* Seconds of unchanged means before trusting the sensor again. */
#define TILT_RATE            8.0f      /* degrees per second */
#define TILT_HOLD_MS         1000u
#define TILT_CATCH_UP_MS     100u      /* Hold on this much longer while gravity is still off. */
#define HORIZONTAL_TAU_S     0.08f     /* About 2 Hz. */
#define GRAVITY_TAU_S        2.0f      /* A gyro zero error under DRIFT_RATE then stays under 35 mg. */
#define GRAVITY_TAU_ABOVE_S  30.0f     /* While above the threshold: braking must not look like a tilt after. */
#define GRAVITY_TAU_TILT_S   1.0f      /* Soon after a move, though a door's shove must not count for much. */
#define BIAS_TAU_S           10.0f
#define DRIFT_RATE           1.0f      /* degrees per second: a whole second slower than this is drift. */
#define GYRO_DISAGREES       0.25f     /* Gravity moved less than this share of what the gyro says. */
#define GYRO_FIX_SHARE       0.5f      /* A real error goes in a few seconds, and noise does little. */
#define NEAR_MISS_FRACTION   0.5f
#define BUCKET_MS            100u
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
    memcpy(m->prev_mean, mean, sizeof(m->prev_mean));
    m->calibrated = true;
}

static bool means_agree(const float a[6], const float b[6]) {
    for (int i = 0; i < 6; i++) {
        if (fabsf(a[i] - b[i]) >= (i < 3 ? SAME_ACCEL : SAME_GYRO)) {
            return false;
        }
    }
    return true;
}

static bool gravity_plausible(const float mean[6]) {
    float g = length(mean);
    return g >= MIN_GRAVITY && g <= MAX_GRAVITY;
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

/* Finds gravity and gyro bias: from 1 s of stillness, or from 2 s averages if stillness
 * does not come. A passenger leaning the car during an average would spoil it, so two
 * in a row must agree, unless the car is really moving and they never will. */
static void calibrate(yg_motion_t *m, uint32_t now, const float v[6]) {
    float mean[6];
    bool still;
    if (window_add(m, v, mean, &still) && still && gravity_plausible(mean)) {
        set_calibration(m, mean);
        return;
    }
    for (int i = 0; i < 6; i++) {
        m->avg_sum[i] += v[i];
    }
    m->avg_n++;
    if (m->avg_n >= AVERAGE_SAMPLES) {
        float avg[6];
        for (int i = 0; i < 6; i++) {
            avg[i] = m->avg_sum[i] / (float)m->avg_n;
            m->avg_sum[i] = 0;
        }
        m->avg_steady = m->avg_valid && means_agree(avg, m->avg_last);
        memcpy(m->avg_last, avg, sizeof(m->avg_last));
        m->avg_n = 0;
        m->avg_valid = true;
    }
    uint32_t waited = now - m->first_ms;
    bool time_up = waited >= CALIBRATE_LAST || (waited >= CALIBRATE_DEADLINE && m->avg_steady);
    if (m->avg_valid && time_up && gravity_plausible(m->avg_last)) {
        set_calibration(m, m->avg_last);
    }
}

/* Once a second the engine's shake has averaged out, and the gyro can be checked. */
static void check_gyro(yg_motion_t *m, const float mean[6]) {
    float glen = length(m->gravity);
    float up[3], rot[3], tilt[3];
    for (int i = 0; i < 3; i++) {
        up[i] = m->gravity[i] / glen;
        rot[i] = mean[i + 3] - m->bias[i];
    }
    float yaw = dot(rot, up);
    for (int i = 0; i < 3; i++) {
        tilt[i] = rot[i] - yaw * up[i];
    }
    float tilt_rate = length(tilt);

    /* The gyro's zero moves as the car warms up, and what little is left is that drift. */
    if (tilt_rate < DRIFT_RATE) {
        for (int i = 0; i < 3; i++) {
            m->bias[i] += tilt[i] * WINDOW_S / BIAS_TAU_S;
        }
        return;
    }

    /* The gyro says the unit is tilting, but gravity stayed where it was, so it is the
     * gyro that is wrong. Around the vertical gravity cannot show this: see the rest. */
    float moved[3];
    for (int i = 0; i < 3; i++) {
        moved[i] = mean[i] - m->prev_mean[i];
    }
    if (length(moved) < GYRO_DISAGREES * tilt_rate * DEG_TO_RAD * WINDOW_S) {
        for (int i = 0; i < 3; i++) {
            m->bias[i] += tilt[i] * GYRO_FIX_SHARE;
            m->gravity[i] = mean[i];
        }
    }
}

/* A unit at rest for a long time tells the truth about gravity and gyro bias, whatever
 * went wrong before, such as calibrating in a bend. Whole-second means are steady with
 * the engine shaking too. Real rotation shows as means that move, except around the
 * vertical at a steady rate, as in a long bend, which this takes for bias until the
 * next rest. */
static void watch_for_rest(yg_motion_t *m, const float mean[6]) {
    if (m->rest_windows == 0 || !means_agree(mean, m->rest_mean)) {
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
    if (dt > 0.1f) {
        /* The unit may have been moved while no samples came. */
        m->tilt_until = now + TILT_HOLD_MS;
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
    if (tilting && horizontal_mg >= threshold) {
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
    bool still;
    if (window_add(m, v, mean, &still)) {
        check_gyro(m, mean);
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
