idf_component_register(SRCS "screenLib.c"
                    INCLUDE_DIRS "."
                    REQUIRES freertos esp_driver_i2c mqttLib
)
