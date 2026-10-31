idf_component_register(
    SRCS "yg_io.c"
    INCLUDE_DIRS "include"
    REQUIRES yg_core
    PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_timer log
)
