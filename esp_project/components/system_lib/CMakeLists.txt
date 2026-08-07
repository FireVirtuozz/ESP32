idf_component_register(
    SRCS "system_lib.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_app_format log_lib esp_partition bootloader_support esp_bootloader_format efuse esp_event esp_mm
)
