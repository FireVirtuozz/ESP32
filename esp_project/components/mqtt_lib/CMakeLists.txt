idf_component_register(
    SRCS "mqtt_lib.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES mqtt actuators_lib nvs_lib ota_lib system_lib wifi_lib screen_lib espressif__cjson cmd_lib log_lib
)
