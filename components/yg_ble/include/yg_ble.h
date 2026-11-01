/* NimBLE as a passive scanner. See the design spec, section 10.3. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "yg_engine.h"

/* Runs in the BLE host task. In open mode only stable iBeacons and the paired
 * beacon are passed on, so a crowded parking lot cannot flood the firmware. */
typedef void (*yg_ble_advert_cb_t)(const yg_addr_t *a, int8_t rssi, const yg_ibeacon_t *ib);

void yg_ble_start(yg_ble_advert_cb_t on_advert);
/* share_radio listens a quarter of the time, because Wi-Fi gets nothing through a scan at full time. */
void yg_ble_scan(yg_scan_mode_t mode, const yg_beacon_t *beacon, bool share_radio);

/* Restarts the scan if it ever stopped. Call about once a second. */
void yg_ble_ensure_scanning(void);

/* True once the stack is up and a requested scan is actually running. */
bool yg_ble_is_scanning(void);

/* Counts the distinct devices heard between these two calls, in open mode. */
void yg_ble_count_begin(void);
uint16_t yg_ble_count_end(void);
bool yg_ble_counted_device(uint16_t index, yg_addr_t *addr, int8_t *rssi);
