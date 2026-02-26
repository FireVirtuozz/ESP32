idf_component_register(
    SRCS "ledLib.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio freertos nvsLib esp_driver_ledc esp_hal_gpio lcdLib logLib
)
