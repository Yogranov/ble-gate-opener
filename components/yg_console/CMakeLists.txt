idf_component_register(
    SRCS "yg_console.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES yg_app yg_ble yg_core yg_io yg_log yg_settings_nvs console esp_system log
)
