/* The hotspot, the HTTP API and firmware upload. */
#pragma once

#include <stdbool.h>

#include "yg_record.h"

void yg_net_init(void);

/* Queued to the network task, so the decision task never waits for Wi-Fi. */
void yg_net_hotspot(bool on);
bool yg_net_hotspot_running(void);

/* The Wi-Fi half of the debug snapshot. */
void yg_net_env_start(void);
void yg_net_env_finish(yg_env_t *out);

/* Adds the "api", "ota-self" and "wifi-join" console commands. */
void yg_net_console_register(void);
