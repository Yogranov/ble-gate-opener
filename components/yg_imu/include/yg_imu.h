/* MPU6050 at 50 Hz over I2C. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "yg_motion.h"

typedef void (*yg_imu_sample_cb_t)(const yg_imu_sample_t *s, float temp_c);
/* absent means the sensor has not been found on the bus. */
typedef void (*yg_imu_failed_cb_t)(bool absent);

/* Sets up the bus and looks for the sensor. Returns false if it is not there yet:
 * sampling keeps looking for it. */
bool yg_imu_init(void);

/* What WHO_AM_I said, which differs on clone chips. 0 until the sensor is found. */
uint8_t yg_imu_id(void);

/* Starts sampling, found or not. The callbacks run in the sampling task. */
void yg_imu_start(yg_imu_sample_cb_t on_sample, yg_imu_failed_cb_t on_failed);
