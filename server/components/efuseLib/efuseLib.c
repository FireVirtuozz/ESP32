#include "efuseLib.h"
#include <hal/efuse_hal.h>

static const char *TAG = "efuse_library";

void print_chip_info() {

    //mac address
    uint8_t mac[6];
    efuse_hal_get_mac(mac);
    ESP_LOGI(TAG, "Factory MAC address : %02X:%02X:%02X:%02X:%02X:%02X",
         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    //block : efuse zone with config bits (firmware compatibility, info, security..)
    //0 if revision block never used
    ESP_LOGI(TAG, "chip block revision : %d", efuse_hal_blk_version());

    //if flash encryption enabled
    ESP_LOGI(TAG, "flash encryption enabled : %s",
        efuse_hal_flash_encryption_enabled() ? "Yes" : "No");

    //Bootloader checks if chip is not too old
    ESP_LOGI(TAG, "Skip maximum version check : %s",
        efuse_hal_get_disable_wafer_version_major() ? "Yes" : "No");

    //Bootloader checks if chip block is not too old
    ESP_LOGI(TAG, "Skip block version check : %s",
        efuse_hal_get_disable_blk_version_major() ? "Yes" : "No");

    //chip version
    ESP_LOGI(TAG, "chip revision : %d", efuse_hal_chip_revision());
    ESP_LOGI(TAG, "major chip version : %d", efuse_hal_get_major_chip_version());
    ESP_LOGI(TAG, "minor chip version : %d", efuse_hal_get_minor_chip_version());

    //chip package
    ESP_LOGI(TAG, "chip package version : %d", efuse_hal_get_chip_ver_pkg());

}