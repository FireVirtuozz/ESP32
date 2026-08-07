idf_component_register(SRCS "lcd_lvgl_lib.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES freertos esp_driver_i2c nvs_lib esp_lcd lvgl__lvgl log_lib esp_timer
)
