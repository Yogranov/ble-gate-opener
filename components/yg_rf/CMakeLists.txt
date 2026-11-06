idf_component_register(
    SRCS "yg_rf.c"
    INCLUDE_DIRS "include"
    REQUIRES yg_core
    PRIV_REQUIRES esp_driver_spi esp_driver_rmt esp_driver_gpio log
)
