/* Stores settings, the beacon and the trip counter in NVS. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "yg_rfcode.h"
#include "yg_settings.h"
#include "yg_types.h"

esp_err_t yg_nvs_init(void);

/* Fills in defaults, then stored values. On first boot this creates the hotspot
 * name and a random password. Returns false if the store could not be read. */
bool yg_nvs_load(yg_settings_t *s, yg_beacon_t *b);

/* Writes only what changed, to spare the flash. */
void yg_nvs_save_settings(const yg_settings_t *s);
void yg_nvs_save_beacon(const yg_beacon_t *b);

/* The learned RF gate code, kept on reset-to-defaults and wiped on factory reset. */
bool yg_nvs_load_rf_code(yg_rfcode_t *c);
void yg_nvs_save_rf_code(const yg_rfcode_t *c);

/* The gyro zero learned at rest, for a start on a car that will not hold still. */
bool yg_nvs_load_gyro_zero(float zero[3]);
void yg_nvs_save_gyro_zero(const float zero[3]);

/* Returns the number of this trip and stores it. */
uint32_t yg_nvs_next_trip(void);
void yg_nvs_set_trip(uint32_t trip);

esp_err_t yg_nvs_factory_reset(void);

/* This unit's default hotspot name, from its address. */
void yg_nvs_default_ssid(char *out, size_t len);

/* For bench tests of rollback: makes the next boot fail its firmware self-check. */
void yg_nvs_set_test_flag(void);
bool yg_nvs_take_test_flag(void);
