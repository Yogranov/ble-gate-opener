idf_component_register(
    SRCS "yg_ble.c"
    INCLUDE_DIRS "include"
    REQUIRES yg_core
    PRIV_REQUIRES bt log
)
