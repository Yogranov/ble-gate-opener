/* Decides whether the motion sensor can be trusted. See the design spec, section 5.5. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "yg_motion.h"

typedef enum
{
    YG_SENSOR_STARTING,
    YG_SENSOR_OK,
    YG_SENSOR_FAILED,
} yg_sensor_state_t;

typedef enum
{
    YG_SENSOR_ERR_NONE,
    YG_SENSOR_ERR_ABSENT,
    YG_SENSOR_ERR_GRAVITY,
    YG_SENSOR_ERR_READS,
    YG_SENSOR_ERR_STUCK,
} yg_sensor_err_t;

typedef struct
{
    yg_sensor_state_t state;
    yg_sensor_err_t err;
    uint16_t start_n;
    float start_sum;
    uint8_t read_fails;
    bool has_last;
    yg_imu_sample_t last;
    uint32_t last_change;
    uint16_t good_run;
} yg_sensor_health_t;

void yg_sensor_health_init(yg_sensor_health_t *h);
void yg_sensor_health_absent(yg_sensor_health_t *h);
void yg_sensor_health_on_sample(yg_sensor_health_t *h, uint32_t now, const yg_imu_sample_t *s);
void yg_sensor_health_on_read_failed(yg_sensor_health_t *h);
