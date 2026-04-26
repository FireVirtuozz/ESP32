idf_component_register(
    SRCS "mqttLib.c"
    INCLUDE_DIRS "."
    REQUIRES mqtt ledLib nvsLib otaLib systemLib wifiLib screenLib espressif__cjson cmdLib logLib
)
