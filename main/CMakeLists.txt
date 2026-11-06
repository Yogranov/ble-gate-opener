set(yg_extra_reqs "")
if(CONFIG_YG_GATE_DRIVER_RF433)
    list(APPEND yg_extra_reqs yg_rf)
endif()

idf_component_register(
    SRCS "main.c"
    PRIV_REQUIRES yg_app yg_console yg_io yg_log yg_net yg_settings_nvs log ${yg_extra_reqs}
)
