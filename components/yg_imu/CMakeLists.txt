idf_component_register(
    SRCS "yg_imu.c"
    INCLUDE_DIRS "include"
    REQUIRES yg_core
    PRIV_REQUIRES esp_driver_i2c log
)
