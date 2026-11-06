#include "esp_err.h"
#include "esp_log.h"
#include "yg_app.h"
#include "yg_console.h"
#include "yg_io.h"
#include "yg_log.h"
#include "yg_net.h"
#include "yg_nvs.h"
#include "sdkconfig.h"
#if CONFIG_YG_GATE_DRIVER_RF433
#include "yg_rf.h"
#endif

void app_main(void) {
    /* First, before anything else can go wrong: the actuator stays idle. */
#if CONFIG_YG_GATE_DRIVER_RF433
    yg_rf_init();
#else
    yg_io_remote_init();
#endif

    esp_log_level_set("NimBLE", ESP_LOG_ERROR);
    ESP_ERROR_CHECK(yg_nvs_init());
    yg_log_init();
    yg_net_init();

    const yg_app_hooks_t hooks = {
        .hotspot = yg_net_hotspot,
        .hotspot_running = yg_net_hotspot_running,
        .env_start = yg_net_env_start,
        .env_finish = yg_net_env_finish,
    };
    yg_app_start(&hooks);
    yg_console_start(yg_net_console_register);
}
