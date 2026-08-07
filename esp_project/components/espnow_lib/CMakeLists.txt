idf_component_register(
    SRCS "espnow_lib.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES log_lib udp_lib cmd_lib esp_event esp_netif esp_wifi
)
