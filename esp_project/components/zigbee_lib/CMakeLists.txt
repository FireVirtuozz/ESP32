idf_component_register(
    SRCS "zigbee_lib.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES log_lib espressif__esp-zigbee-lib nvs_flash 
)
