idf_component_register(
    SRCS "yg_app.c"
    INCLUDE_DIRS "include"
    REQUIRES yg_core
    PRIV_REQUIRES yg_ble yg_imu yg_io yg_log yg_settings_nvs yg_rf app_update esp_system esp_timer log
)
