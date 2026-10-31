#include "yg_sensor_health.h"

#include <math.h>
#include <string.h>

#define START_SAMPLES   25
#define GRAVITY_TOLERANCE 0.3f
#define MAX_READ_FAILS  10
#define STUCK_MS        5000u
#define RECOVERY_RUN    250

static float magnitude(const yg_imu_sample_t *s) {
    return sqrtf(s->ax * s->ax + s->ay * s->ay + s->az * s->az);
}

static bool same_sample(const yg_imu_sample_t *a, const yg_imu_sample_t *b) {
    return memcmp(a, b, sizeof(*a)) == 0;
}

static void fail(yg_sensor_health_t *h, yg_sensor_err_t err) {
    h->state = YG_SENSOR_FAILED;
    h->err = err;
    h->good_run = 0;
}

void yg_sensor_health_init(yg_sensor_health_t *h) {
    memset(h, 0, sizeof(*h));
    h->state = YG_SENSOR_STARTING;
}

void yg_sensor_health_absent(yg_sensor_health_t *h) {
    fail(h, YG_SENSOR_ERR_ABSENT);
}

void yg_sensor_health_on_sample(yg_sensor_health_t *h, uint32_t now, const yg_imu_sample_t *s) {
    if (h->err == YG_SENSOR_ERR_ABSENT) {
        return;
    }
    h->read_fails = 0;

    bool changed = !h->has_last || !same_sample(&h->last, s);
    if (changed) {
        h->last = *s;
        h->has_last = true;
        h->last_change = now;
    } else if (now - h->last_change >= STUCK_MS && h->state != YG_SENSOR_FAILED) {
        fail(h, YG_SENSOR_ERR_STUCK);
        return;
    }

    if (h->state == YG_SENSOR_STARTING) {
        h->start_sum += magnitude(s);
        h->start_n++;
        if (h->start_n >= START_SAMPLES) {
            float mean = h->start_sum / (float)h->start_n;
            if (fabsf(mean - 1.0f) <= GRAVITY_TOLERANCE) {
                h->state = YG_SENSOR_OK;
            } else {
                fail(h, YG_SENSOR_ERR_GRAVITY);
            }
        }
        return;
    }

    if (h->state == YG_SENSOR_FAILED) {
        bool sane = fabsf(magnitude(s) - 1.0f) <= GRAVITY_TOLERANCE;
        h->good_run = changed && sane ? (uint16_t)(h->good_run + 1) : 0;
        if (h->good_run >= RECOVERY_RUN) {
            h->state = YG_SENSOR_OK;
            h->err = YG_SENSOR_ERR_NONE;
            h->good_run = 0;
        }
    }
}

void yg_sensor_health_on_read_failed(yg_sensor_health_t *h) {
    if (h->err == YG_SENSOR_ERR_ABSENT) {
        return;
    }
    h->good_run = 0;
    if (h->read_fails < MAX_READ_FAILS) {
        h->read_fails++;
    }
    if (h->read_fails >= MAX_READ_FAILS && h->state != YG_SENSOR_FAILED) {
        fail(h, YG_SENSOR_ERR_READS);
    }
}
