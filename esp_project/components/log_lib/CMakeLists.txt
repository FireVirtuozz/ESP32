idf_component_register(
    SRCS "log_lib.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES freertos udp_lib esp_timer espnow_lib
)
