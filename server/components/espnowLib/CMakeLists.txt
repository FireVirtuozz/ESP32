idf_component_register(
    SRCS "espnowLib.c"
    INCLUDE_DIRS "."
    REQUIRES logLib udpLib cmdLib
)
