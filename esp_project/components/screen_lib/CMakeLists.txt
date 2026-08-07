idf_component_register(SRCS "screen_lib.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES freertos esp_driver_i2c nvs_lib log_lib
)
