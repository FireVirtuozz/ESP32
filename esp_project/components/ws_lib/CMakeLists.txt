idf_component_register(
    SRCS "ws_lib.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_http_server esp_wifi nvs_flash actuators_lib log_lib
)
