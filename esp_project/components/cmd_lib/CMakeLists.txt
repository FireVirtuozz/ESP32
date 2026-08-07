idf_component_register(
    SRCS "cmd_lib.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES log_lib actuators_lib
)
