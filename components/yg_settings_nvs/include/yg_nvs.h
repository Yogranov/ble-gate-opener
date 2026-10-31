/* Stores settings, the beacon and the trip counter in NVS. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "yg_settings.h"
#include "yg_types.h"

esp_err_t yg_nvs_init(void);

/* Fills in defaults, then stored values. On first boot this creates the hotspot
 * name and a random password. */
void yg_nvs_load(yg_settings_t *s, yg_beacon_t *b);

/* Writes only what changed, to spare the flash. */
void yg_nvs_save_settings(const yg_settings_t *s);
void yg_nvs_save_beacon(const yg_beacon_t *b);

/* Returns the number of this trip and stores it. */
uint32_t yg_nvs_next_trip(void);

esp_err_t yg_nvs_factory_reset(void);

/* For bench tests of rollback: makes the next boot fail its firmware self-check. */
void yg_nvs_set_test_flag(void);
bool yg_nvs_take_test_flag(void);
