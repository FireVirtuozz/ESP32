idf_component_register(
    SRCS "camera_lib.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES actuators_lib log_lib udp_lib esp_psram espressif__esp32-camera esp_timer
)
