idf_component_register(
    SRCS "yg_net.c" "yg_http.c"
    INCLUDE_DIRS "include"
    REQUIRES yg_core
    PRIV_REQUIRES yg_app yg_io yg_log yg_settings_nvs
                  app_update bootloader_support console esp_http_server esp_netif esp_partition esp_timer esp_wifi lwip log
)

# The page is embedded gzipped, so a phone with no internet gets everything from the unit.
set(WEB_SOURCE "${PROJECT_DIR}/web/index.html")
set(WEB_GZIP "${CMAKE_CURRENT_BINARY_DIR}/index.html.gz")
add_custom_command(
    OUTPUT "${WEB_GZIP}"
    COMMAND ${PYTHON} "${PROJECT_DIR}/tools/gzip_file.py" "${WEB_SOURCE}" "${WEB_GZIP}"
    DEPENDS "${WEB_SOURCE}" "${PROJECT_DIR}/tools/gzip_file.py"
    VERBATIM
)
add_custom_target(yg_web_gzip DEPENDS "${WEB_GZIP}")
add_dependencies(${COMPONENT_LIB} yg_web_gzip)
target_add_binary_data(${COMPONENT_LIB} "${WEB_GZIP}" BINARY DEPENDS yg_web_gzip)
