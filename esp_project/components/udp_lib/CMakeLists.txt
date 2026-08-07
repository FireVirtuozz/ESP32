idf_component_register(
    SRCS "udp_lib.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES nvs_flash esp_timer actuators_lib cmd_lib log_lib sensors_lib camera_lib ota_lib
)
