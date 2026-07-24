idf_component_register(
    SRCS "zigbeeLib.c"
    INCLUDE_DIRS "."
    REQUIRES logLib espressif__esp-zigbee-lib
)
