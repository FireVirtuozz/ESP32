idf_component_register(
    SRCS "udpLib.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash ledLib esp_timer
)
