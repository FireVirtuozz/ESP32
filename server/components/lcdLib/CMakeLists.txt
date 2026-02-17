idf_component_register(SRCS "lcdLib.c"
                    INCLUDE_DIRS "."
                    REQUIRES freertos esp_driver_i2c mqttLib nvsLib esp_lcd lvgl__lvgl
)
