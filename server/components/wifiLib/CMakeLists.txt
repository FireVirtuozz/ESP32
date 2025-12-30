idf_component_register(
    SRCS "wifiLib.c"
    INCLUDE_DIRS "."
    REQUIRES freertos esp_wifi esp_netif nvs_flash mqttLib
)
