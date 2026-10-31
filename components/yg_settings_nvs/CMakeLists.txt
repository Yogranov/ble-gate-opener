idf_component_register(
    SRCS "yg_nvs.c"
    INCLUDE_DIRS "include"
    REQUIRES yg_core nvs_flash
    PRIV_REQUIRES bootloader_support esp_hw_support esp_system log
)
