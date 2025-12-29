idf_component_register(
    SRCS "otaLib.c"
    INCLUDE_DIRS "."
    REQUIRES app_update esp_https_ota
)
