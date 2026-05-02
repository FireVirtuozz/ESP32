idf_component_register(
    SRCS "logLib.c"
    INCLUDE_DIRS "."
    REQUIRES freertos udpLib esp_timer
)
