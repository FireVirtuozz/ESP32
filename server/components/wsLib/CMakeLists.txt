idf_component_register(
    SRCS "wsLib.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_server esp_wifi nvs_flash ledLib
)
