idf_component_register(
    SRCS "yg_log.c"
    INCLUDE_DIRS "include"
    REQUIRES yg_core
    PRIV_REQUIRES esp_partition log
)
