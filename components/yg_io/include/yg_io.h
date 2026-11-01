/* The remote output, LED, buzzer and button. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "yg_engine.h"
#include "yg_gesture.h"

/* Call this first in app_main: it drives the remote output low. */
void yg_io_remote_init(void);

/* Presses the remote. A timer ends the press, whatever else the firmware is doing. */
void yg_io_press(uint32_t ms);

/* The level on the remote pin right now, read back from the pad. */
bool yg_io_remote_level(void);

typedef void (*yg_io_gesture_cb_t)(yg_gesture_t gesture);

/* Starts the task that runs the LED, buzzer and button. */
void yg_io_start(yg_io_gesture_cb_t on_gesture);

void yg_io_led(yg_led_event_t e);
void yg_io_buzz(yg_buzz_event_t e, uint8_t buzzer_type);

/* For the page's test button, which works whatever the settings say. */
void yg_io_test_led(void);
