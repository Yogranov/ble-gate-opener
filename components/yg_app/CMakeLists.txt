set(yg_app_extra "")
if(CONFIG_YG_GATE_DRIVER_RF433)
    list(APPEND yg_app_extra yg_rf)
endif()

idf_component_register(
    SRCS "yg_app.c"
    INCLUDE_DIRS "include"
    REQUIRES yg_core
    PRIV_REQUIRES yg_ble yg_imu yg_io yg_log yg_settings_nvs app_update esp_system esp_timer log ${yg_app_extra}
)
