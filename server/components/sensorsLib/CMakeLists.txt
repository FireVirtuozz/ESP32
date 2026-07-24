idf_component_register(
    SRCS "sensorsLib.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio logLib esp_timer esp_driver_i2c esp_adc udpLib espnowLib esp_driver_spi esp_driver_pcnt esp_driver_rmt esp_driver_tsens 
)
