#include <math.h>
#include <stdlib.h>

#include "yg_motion.h"
#include "yg_test.h"

#define DT_MS 20
#define PI    3.14159265f

/* Attitude maps the car frame (x forward, y left, z up) to the sensor frame. */
typedef struct { float m[3][3]; } mat3;

static mat3 identity(void) {
    mat3 r = { { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };
    return r;
}

static mat3 mul(mat3 a, mat3 b) {
    mat3 r;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
        }
    }
    return r;
}

static mat3 rot_x(float deg) {
    float c = cosf(deg * PI / 180), s = sinf(deg * PI / 180);
    mat3 r = { { { 1, 0, 0 }, { 0, c, -s }, { 0, s, c } } };
    return r;
}

static mat3 rot_y(float deg) {
    float c = cosf(deg * PI / 180), s = sinf(deg * PI / 180);
    mat3 r = { { { c, 0, s }, { 0, 1, 0 }, { -s, 0, c } } };
    return r;
}

static void apply(const mat3 *r, const float v[3], float out[3]) {
    for (int i = 0; i < 3; i++) {
        out[i] = r->m[i][0] * v[0] + r->m[i][1] * v[1] + r->m[i][2] * v[2];
    }
}

static const float BIAS[3] = { 10.0f, -7.0f, 4.0f };

typedef struct {
    yg_motion_t m;
    yg_settings_t s;
    mat3 attitude;
    uint32_t now;
    yg_motion_cause_t first_cause;
    uint32_t first_cause_at;
    bool any_near_miss;
    yg_motion_out_t out;
} rig_t;

static float noise(float amp) {
    return amp * ((float)rand() / (float)RAND_MAX * 2.0f - 1.0f);
}

/* One sample: linear acceleration in g and rotation in deg/s, both in the car frame. */
static void step(rig_t *r, const float accel[3], const float gyro[3]) {
    float car_a[3] = { accel[0], accel[1], accel[2] + 1.0f };
    yg_imu_sample_t in;
    float a[3], g[3];
    apply(&r->attitude, car_a, a);
    apply(&r->attitude, gyro, g);
    in.ax = a[0] + noise(0.002f);
    in.ay = a[1] + noise(0.002f);
    in.az = a[2] + noise(0.002f);
    in.gx = g[0] + BIAS[0] + noise(0.1f);
    in.gy = g[1] + BIAS[1] + noise(0.1f);
    in.gz = g[2] + BIAS[2] + noise(0.1f);
    yg_motion_update(&r->m, &r->s, r->now, &in, &r->out);
    if (r->out.cause != YG_MOTION_NONE && r->first_cause == YG_MOTION_NONE) {
        r->first_cause = r->out.cause;
        r->first_cause_at = r->now;
    }
    if (r->out.near_miss) r->any_near_miss = true;
    r->now += DT_MS;
}

static void still(rig_t *r, uint32_t ms) {
    const float zero[3] = { 0, 0, 0 };
    for (uint32_t t = 0; t < ms; t += DT_MS) step(r, zero, zero);
}

static void rig_init(rig_t *r, mat3 attitude) {
    memset(r, 0, sizeof(*r));
    yg_settings_defaults(&r->s);
    yg_motion_init(&r->m);
    r->attitude = attitude;
    r->now = 1000;
}

static void reset_seen(rig_t *r) {
    r->first_cause = YG_MOTION_NONE;
    r->any_near_miss = false;
}

static mat3 orientation(int i) {
    switch (i) {
    case 0: return identity();
    case 1: return mul(rot_x(35), rot_y(50));
    case 2: return mul(rot_x(170), rot_y(-20));
    default: return rot_y(90);   /* Standing on its side. */
    }
}
#define ORIENTATIONS 4

static void still_car_calibrates_fast_and_stays_quiet(void) {
    for (int o = 0; o < ORIENTATIONS; o++) {
        static rig_t r;
        rig_init(&r, orientation(o));
        still(&r, 1300);
        CHECK(r.out.calibrated);
        still(&r, 60000);
        CHECK_INT(r.first_cause, YG_MOTION_NONE);
    }
}

static void no_motion_is_reported_before_calibration(void) {
    static rig_t r;
    rig_init(&r, identity());
    const float a[3] = { 0.3f, 0, 0 };
    const float g[3] = { 0, 0, 40 };
    for (int i = 0; i < 20; i++) step(&r, a, g);
    CHECK(!r.out.calibrated);
    CHECK_INT(r.first_cause, YG_MOTION_NONE);
}

static void engine_shake_is_ignored_and_calibration_falls_back(void) {
    static rig_t r;
    rig_init(&r, orientation(1));
    uint32_t calibrated_at = 0;
    for (uint32_t t = 0; t < 60000; t += DT_MS) {
        float phase = 2 * PI * 23.0f * (float)t / 1000.0f;
        float a[3] = { 0.03f * sinf(phase), 0.03f * cosf(phase), 0.03f * sinf(phase + 1) };
        float g[3] = { 2.0f * sinf(phase), 2.0f * cosf(phase), 2.0f * sinf(phase + 2) };
        step(&r, a, g);
        if (r.out.calibrated && calibrated_at == 0) calibrated_at = t + DT_MS;
    }
    CHECK(calibrated_at > 0);
    CHECK(calibrated_at <= 10100);
    CHECK_INT(r.first_cause, YG_MOTION_NONE);
}

static void pulling_out_reports_acceleration_within_a_second(void) {
    for (int o = 0; o < ORIENTATIONS; o++) {
        static rig_t r;
        rig_init(&r, orientation(o));
        still(&r, 5000);
        uint32_t start = r.now;
        const float a[3] = { 0.1f, 0, 0 };
        const float g[3] = { 0, 0, 0 };
        for (int i = 0; i < 100; i++) step(&r, a, g);
        CHECK_INT(r.first_cause, YG_MOTION_ACCEL);
        CHECK(r.first_cause_at - start <= 1000);
    }
}

static void reversing_is_detected_too(void) {
    static rig_t r;
    rig_init(&r, orientation(2));
    still(&r, 5000);
    const float a[3] = { -0.08f, 0, 0 };
    const float g[3] = { 0, 0, 0 };
    for (int i = 0; i < 100; i++) step(&r, a, g);
    CHECK_INT(r.first_cause, YG_MOTION_ACCEL);
}

static void gentle_creep_below_threshold_is_not_acceleration(void) {
    static rig_t r;
    rig_init(&r, identity());
    still(&r, 5000);
    const float a[3] = { 0.03f, 0, 0 };
    const float g[3] = { 0, 0, 0 };
    for (int i = 0; i < 250; i++) step(&r, a, g);
    CHECK_INT(r.first_cause, YG_MOTION_NONE);
}

static void short_push_below_time_threshold_is_ignored(void) {
    static rig_t r;
    rig_init(&r, identity());
    still(&r, 5000);
    const float a[3] = { 0.1f, 0, 0 };
    const float g[3] = { 0, 0, 0 };
    for (int i = 0; i < 25; i++) step(&r, a, g);   /* 500 ms */
    still(&r, 5000);
    CHECK_INT(r.first_cause, YG_MOTION_NONE);
    CHECK(r.any_near_miss);
}

static void turning_reports_yaw_within_a_second(void) {
    for (int o = 0; o < ORIENTATIONS; o++) {
        for (int dir = -1; dir <= 1; dir += 2) {
            static rig_t r;
            rig_init(&r, orientation(o));
            still(&r, 5000);
            uint32_t start = r.now;
            const float a[3] = { 0, 0, 0 };
            const float g[3] = { 0, 0, 20.0f * (float)dir };
            for (int i = 0; i < 50; i++) step(&r, a, g);
            CHECK_INT(r.first_cause, YG_MOTION_YAW);
            CHECK(r.first_cause_at - start <= 1000);
        }
    }
}

static void slow_rotation_outside_the_window_is_not_a_turn(void) {
    static rig_t r;
    rig_init(&r, identity());
    still(&r, 5000);
    const float a[3] = { 0, 0, 0 };
    const float g[3] = { 0, 0, 1.0f };   /* 3 degrees per 3 s window. */
    for (int i = 0; i < 1000; i++) step(&r, a, g);
    CHECK_INT(r.first_cause, YG_MOTION_NONE);
}

static void door_slam_is_a_near_miss_not_motion(void) {
    for (int o = 0; o < ORIENTATIONS; o++) {
        static rig_t r;
        rig_init(&r, orientation(o));
        still(&r, 5000);
        /* A hard shove that dies away in 0.2 s, then the body bounces at 1.5 Hz. */
        for (uint32_t t = 0; t < 200; t += DT_MS) {
            float k = expf(-(float)t / 50.0f);
            float a[3] = { 0.1f * k, 0.3f * k, 0.3f * k };
            float g[3] = { 5.0f * k, 5.0f * k, 2.0f * k };
            step(&r, a, g);
        }
        for (uint32_t t = 0; t < 2000; t += DT_MS) {
            float k = expf(-(float)t / 600.0f) * sinf(2 * PI * 1.5f * (float)t / 1000.0f);
            float a[3] = { 0, 0.015f * k, 0.03f * k };
            float g[3] = { 1.0f * k, 0, 0 };
            step(&r, a, g);
        }
        still(&r, 3000);
        CHECK_INT(r.first_cause, YG_MOTION_NONE);
        CHECK(r.any_near_miss);
    }
}

/* Rolls or pitches the sensor by `deg` over `ms` around a horizontal car axis. */
static void tilt(rig_t *r, float deg, uint32_t ms, int axis) {
    float rate = deg / ((float)ms / 1000.0f);
    float done = 0;
    mat3 base = r->attitude;
    for (uint32_t t = 0; t < ms; t += DT_MS) {
        done += rate * DT_MS / 1000.0f;
        /* Gravity seen by the sensor turns as it rotates; the gyro reads the rate. */
        mat3 turn = axis == 0 ? rot_x(-done) : rot_y(-done);
        r->attitude = mul(base, turn);
        const float a[3] = { 0, 0, 0 };
        float g[3] = { axis == 0 ? rate : 0, axis == 1 ? rate : 0, 0 };
        float sensor_g[3];
        apply(&r->attitude, g, sensor_g);
        yg_imu_sample_t in;
        float car_a[3] = { a[0], a[1], 1.0f }, sensor_a[3];
        apply(&r->attitude, car_a, sensor_a);
        in.ax = sensor_a[0]; in.ay = sensor_a[1]; in.az = sensor_a[2];
        in.gx = sensor_g[0] + BIAS[0]; in.gy = sensor_g[1] + BIAS[1]; in.gz = sensor_g[2] + BIAS[2];
        yg_motion_update(&r->m, &r->s, r->now, &in, &r->out);
        if (r->out.cause != YG_MOTION_NONE && r->first_cause == YG_MOTION_NONE) {
            r->first_cause = r->out.cause;
        }
        r->now += DT_MS;
    }
}

static void passenger_getting_in_is_ignored(void) {
    for (int o = 0; o < ORIENTATIONS; o++) {
        for (int axis = 0; axis < 2; axis++) {
            static rig_t r;
            rig_init(&r, orientation(o));
            still(&r, 5000);
            tilt(&r, 1.5f, 500, axis);
            still(&r, 30000);
            CHECK_INT(r.first_cause, YG_MOTION_NONE);
        }
    }
}

static void opening_the_glove_box_is_ignored(void) {
    for (int o = 0; o < ORIENTATIONS; o++) {
        for (int axis = 0; axis < 2; axis++) {
            static rig_t r;
            rig_init(&r, orientation(o));
            still(&r, 5000);
            tilt(&r, 45.0f, 1000, axis);
            still(&r, 20000);
            tilt(&r, -45.0f, 800, axis);
            still(&r, 20000);
            CHECK_INT(r.first_cause, YG_MOTION_NONE);
        }
    }
}

static void shove(rig_t *r, float g) {
    for (uint32_t t = 0; t < 200; t += DT_MS) {
        float k = expf(-(float)t / 50.0f);
        float a[3] = { 0, g * k, 0 };
        const float rot[3] = { 0, 0, 0 };
        step(r, a, rot);
    }
}

static void lean_then_door_shut_is_not_motion(void) {
    /* Someone sits down and the car leans, then a door shuts just after. */
    static const float LEANS[] = { 1.5f, 2.0f, 2.2f, 2.6f, 3.5f, 5.0f };
    static const uint32_t GAPS[] = { 500, 1000, 2000 };
    for (int o = 0; o < ORIENTATIONS; o++) {
        for (size_t l = 0; l < sizeof(LEANS) / sizeof(LEANS[0]); l++) {
            for (size_t g = 0; g < sizeof(GAPS) / sizeof(GAPS[0]); g++) {
                static rig_t r;
                rig_init(&r, orientation(o));
                still(&r, 5000);
                tilt(&r, LEANS[l], 500, o % 2);
                still(&r, GAPS[g]);
                shove(&r, 0.15f);
                still(&r, 5000);
                CHECK_INT(r.first_cause, YG_MOTION_NONE);
            }
        }
    }
}

static void slow_glove_box_is_ignored(void) {
    /* Below the rate that counts as tilting. */
    static const uint32_t TIMES[] = { 6000, 14000, 30000 };
    for (int o = 0; o < ORIENTATIONS; o++) {
        for (size_t i = 0; i < sizeof(TIMES) / sizeof(TIMES[0]); i++) {
            for (int axis = 0; axis < 2; axis++) {
                static rig_t r;
                rig_init(&r, orientation(o));
                still(&r, 5000);
                tilt(&r, 42.0f, TIMES[i], axis);
                still(&r, 10000);
                tilt(&r, -42.0f, TIMES[i], axis);
                still(&r, 10000);
                CHECK_INT(r.first_cause, YG_MOTION_NONE);
            }
        }
    }
}

static void motion_ends_soon_after_the_car_stops(void) {
    for (int o = 0; o < ORIENTATIONS; o++) {
        static rig_t r;
        rig_init(&r, orientation(o));
        still(&r, 5000);
        const float a[3] = { -0.2f, 0, 0 };
        const float g[3] = { 0, 0, 0 };
        for (int i = 0; i < 200; i++) step(&r, a, g);
        CHECK_INT(r.first_cause, YG_MOTION_ACCEL);
        still(&r, 1000);
        reset_seen(&r);
        still(&r, 20000);
        CHECK_INT(r.first_cause, YG_MOTION_NONE);
    }
}

static void booting_in_a_steady_bend_heals_once_parked(void) {
    /* Calibrating during a steady turn takes the turn for sensor bias. */
    static rig_t r;
    rig_init(&r, orientation(1));
    const float a[3] = { 0, 0, 0 };
    const float g[3] = { 0, 0, 6.0f };
    for (int i = 0; i < 250; i++) step(&r, a, g);
    CHECK(r.out.calibrated);
    still(&r, 35000);
    reset_seen(&r);
    still(&r, 30000);
    CHECK_INT(r.first_cause, YG_MOTION_NONE);
    const float turn[3] = { 0, 0, 25.0f };
    for (int i = 0; i < 75; i++) step(&r, a, turn);
    CHECK_INT(r.first_cause, YG_MOTION_YAW);
}

static void slow_sensor_drift_is_followed(void) {
    /* The gyro's zero moves as the car warms up. */
    for (int o = 0; o < ORIENTATIONS; o++) {
        static rig_t r;
        rig_init(&r, orientation(o));
        still(&r, 5000);
        const float a[3] = { 0, 0, 0 };
        for (uint32_t t = 0; t < 300000; t += DT_MS) {
            float d = 3.0f * (float)t / 300000.0f;
            const float g[3] = { d, -d, d / 2.0f };
            step(&r, a, g);
        }
        CHECK_INT(r.first_cause, YG_MOTION_NONE);
    }
}

static void a_small_shift_of_the_gyro_zero_is_ignored(void) {
    for (int o = 0; o < ORIENTATIONS; o++) {
        static rig_t r;
        rig_init(&r, orientation(o));
        still(&r, 5000);
        const float a[3] = { 0, 0, 0 };
        const float g[3] = { 0.8f, 0, 0 };
        for (int i = 0; i < 3000; i++) step(&r, a, g);
        CHECK_INT(r.first_cause, YG_MOTION_NONE);
    }
}

static void drift_is_followed_even_with_the_engine_shaking(void) {
    /* The shake leaves no quiet second to start over from. */
    for (int o = 0; o < ORIENTATIONS; o++) {
        static rig_t r;
        rig_init(&r, orientation(o));
        still(&r, 5000);
        for (uint32_t t = 0; t < 300000; t += DT_MS) {
            float phase = 2 * PI * 23.0f * (float)t / 1000.0f;
            float d = 3.0f * (float)t / 300000.0f;
            float a[3] = { 0.03f * sinf(phase), 0.03f * cosf(phase), 0.03f * sinf(phase + 1) };
            float g[3] = { d + 2.0f * sinf(phase), -d + 2.0f * cosf(phase), 2.0f * sinf(phase + 2) };
            step(&r, a, g);
        }
        CHECK_INT(r.first_cause, YG_MOTION_NONE);
    }
}

static void turn_is_still_detected_after_the_glove_box_moved(void) {
    static rig_t r;
    rig_init(&r, identity());
    still(&r, 5000);
    tilt(&r, 45.0f, 1000, 1);
    still(&r, 5000);
    reset_seen(&r);
    const float a[3] = { 0, 0, 0 };
    const float g[3] = { 0, 0, 25.0f };
    for (int i = 0; i < 50; i++) step(&r, a, g);
    CHECK_INT(r.first_cause, YG_MOTION_YAW);
}

static void already_moving_at_boot_detects_later_turns(void) {
    static rig_t r;
    rig_init(&r, orientation(1));
    for (uint32_t t = 0; t < 15000; t += DT_MS) {
        float a[3] = { 0.05f * sinf((float)t / 300.0f), 0.05f * cosf((float)t / 200.0f), 0.02f * sinf((float)t / 50.0f) };
        float g[3] = { 0, 0, 8.0f * sinf((float)t / 900.0f) };
        step(&r, a, g);
    }
    CHECK(r.out.calibrated);
    still(&r, 3000);
    reset_seen(&r);
    const float a[3] = { 0, 0, 0 };
    const float g[3] = { 0, 0, 25.0f };
    for (int i = 0; i < 75; i++) step(&r, a, g);
    CHECK_INT(r.first_cause, YG_MOTION_YAW);
}

static void thresholds_come_from_settings(void) {
    static rig_t r;
    rig_init(&r, identity());
    r.s.motion_yaw_deg = 90;
    r.s.motion_accel_mg = 300;
    still(&r, 5000);
    const float a[3] = { 0.1f, 0, 0 };
    const float g[3] = { 0, 0, 20.0f };
    for (int i = 0; i < 100; i++) step(&r, a, g);
    CHECK_INT(r.first_cause, YG_MOTION_NONE);
    r.s.motion_accel_mg = 40;
    r.s.motion_accel_ms = 200;
    reset_seen(&r);
    still(&r, 3000);
    uint32_t start = r.now;
    const float g0[3] = { 0, 0, 0 };
    for (int i = 0; i < 30; i++) step(&r, a, g0);
    CHECK_INT(r.first_cause, YG_MOTION_ACCEL);
    CHECK(r.first_cause_at - start <= 400);
}

static void peaks_report_strongest_then_clear(void) {
    static rig_t r;
    rig_init(&r, identity());
    still(&r, 5000);
    uint16_t accel, yaw;
    yg_motion_take_peaks(&r.m, &accel, &yaw);
    const float a[3] = { 0.1f, 0, 0 };
    const float g[3] = { 0, 0, 20.0f };
    for (int i = 0; i < 50; i++) step(&r, a, g);
    yg_motion_take_peaks(&r.m, &accel, &yaw);
    CHECK(accel >= 80 && accel <= 120);
    CHECK(yaw >= 18 && yaw <= 22);
    yg_motion_take_peaks(&r.m, &accel, &yaw);
    CHECK_INT(accel, 0);
    CHECK_INT(yaw, 0);
}

int main(void) {
    RUN(still_car_calibrates_fast_and_stays_quiet);
    RUN(no_motion_is_reported_before_calibration);
    RUN(engine_shake_is_ignored_and_calibration_falls_back);
    RUN(pulling_out_reports_acceleration_within_a_second);
    RUN(reversing_is_detected_too);
    RUN(gentle_creep_below_threshold_is_not_acceleration);
    RUN(short_push_below_time_threshold_is_ignored);
    RUN(turning_reports_yaw_within_a_second);
    RUN(slow_rotation_outside_the_window_is_not_a_turn);
    RUN(door_slam_is_a_near_miss_not_motion);
    RUN(passenger_getting_in_is_ignored);
    RUN(opening_the_glove_box_is_ignored);
    RUN(lean_then_door_shut_is_not_motion);
    RUN(slow_glove_box_is_ignored);
    RUN(motion_ends_soon_after_the_car_stops);
    RUN(booting_in_a_steady_bend_heals_once_parked);
    RUN(slow_sensor_drift_is_followed);
    RUN(a_small_shift_of_the_gyro_zero_is_ignored);
    RUN(drift_is_followed_even_with_the_engine_shaking);
    RUN(turn_is_still_detected_after_the_glove_box_moved);
    RUN(already_moving_at_boot_detects_later_turns);
    RUN(thresholds_come_from_settings);
    RUN(peaks_report_strongest_then_clear);
    return TEST_RESULT();
}
