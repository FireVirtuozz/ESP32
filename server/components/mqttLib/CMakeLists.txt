idf_component_register(
    SRCS "mqttLib.c"
    INCLUDE_DIRS "."
    REQUIRES mqtt ledLib nvsLib efuseLib otaLib wifiLib screenLib
)
