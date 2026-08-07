idf_component_register(
    SRCS "wifi_lib.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES freertos esp_wifi esp_netif nvs_lib log_lib
)
