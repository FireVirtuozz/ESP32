idf_component_register(
    SRCS "systemLib.c"
    INCLUDE_DIRS "."
    REQUIRES esp_app_format logLib esp_partition bootloader_support esp_bootloader_format efuse esp_event esp_mm
)
