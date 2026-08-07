idf_component_register(
    SRCS "nvs_lib.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES nvs_flash freertos mqtt_lib log_lib
)
