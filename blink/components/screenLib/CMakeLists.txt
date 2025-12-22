idf_component_register(SRCS "screenLib.c"
                    INCLUDE_DIRS "."
                    REQUIRES spi_flash esp_partition freertos esp_driver_gpio esp_driver_uart esp_http_server
                    PRIV_REQUIRES driver esp_driver_i2c)
