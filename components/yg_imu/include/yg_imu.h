/* MPU6050 at 50 Hz over I2C. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "yg_motion.h"

typedef void (*yg_imu_sample_cb_t)(const yg_imu_sample_t *s, float temp_c);
typedef void (*yg_imu_failed_cb_t)(void);

/* Looks for the sensor and configures it. Returns false if it is not there.
 * id receives what WHO_AM_I said, which differs on clone chips. */
bool yg_imu_init(uint8_t *id);

/* Starts sampling. The callbacks run in the sampling task. */
void yg_imu_start(yg_imu_sample_cb_t on_sample, yg_imu_failed_cb_t on_failed);
