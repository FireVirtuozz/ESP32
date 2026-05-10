idf_component_register(
    SRCS "cameraLib.c"
    INCLUDE_DIRS "."
    REQUIRES ledLib logLib udpLib esp_psram espressif__esp32-camera esp_timer
)
