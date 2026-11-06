/* The 433 MHz CC1101 actuator: learns a fixed-code remote and replays it.
 * Thin hardware layer, like yg_io. Built only on the RF433 variant. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "yg_rfcode.h"

/* One captured burst of edge durations in microseconds; first_high is true when
 * durations[0] is carrier-on. Called from the RF receive task, not an ISR. */
typedef void (*yg_rf_capture_cb_t)(const uint16_t *durations, uint16_t n, bool first_high);

/* Brings up SPI and the CC1101 and leaves it idle with CSn high. Call first in
 * app_main, the way yg_io_remote_init drives the MOSFET low first. */
void yg_rf_init(void);

/* True if the CC1101 answered its version register over SPI. */
bool yg_rf_present(void);

/* Transmit the learned code for `ms`, as a held remote button would. */
void yg_rf_transmit(const yg_rfcode_t *code, uint32_t ms);

/* Start or stop OOK receive. While on, each completed burst goes to `cb`. */
void yg_rf_listen(bool on, yg_rf_capture_cb_t cb);
