#include "efuseLib.h"
#include <hal/efuse_hal.h>
#include "mqttLib.h"

static const char *TAG = "efuse_library";

void print_chip_info() {

    log_mqtt(LOG_INFO, TAG, false, "====================== EFUSE info ======================");

    //mac address
    uint8_t mac[6];
    efuse_hal_get_mac(mac);
    log_mqtt(LOG_INFO, TAG, true, "Factory MAC address : %02X:%02X:%02X:%02X:%02X:%02X",
         mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);

    //block : efuse zone with config bits (firmware compatibility, info, security..)
    //0 if revision block never used
    log_mqtt(LOG_INFO, TAG, true, "chip block revision : %d", efuse_hal_blk_version());

    //if flash encryption enabled
    log_mqtt(LOG_INFO, TAG, true, "flash encryption enabled : %s",
        efuse_hal_flash_encryption_enabled() ? "Yes" : "No");

    //Bootloader checks if chip is not too old
    log_mqtt(LOG_INFO, TAG, true, "Skip maximum version check : %s",
        efuse_hal_get_disable_wafer_version_major() ? "Yes" : "No");

    //Bootloader checks if chip block is not too old
    log_mqtt(LOG_INFO, TAG, true, "Skip block version check : %s",
        efuse_hal_get_disable_blk_version_major() ? "Yes" : "No");

    //chip version
    log_mqtt(LOG_INFO, TAG, true, "chip revision : %d", efuse_hal_chip_revision());
    log_mqtt(LOG_INFO, TAG, true, "major chip version : %d", efuse_hal_get_major_chip_version());
    log_mqtt(LOG_INFO, TAG, true, "minor chip version : %d", efuse_hal_get_minor_chip_version());

    //chip package
    log_mqtt(LOG_INFO, TAG, true, "chip package version : %d", efuse_hal_get_chip_ver_pkg());

    log_mqtt(LOG_INFO, TAG, false, "===================== EFUSE info end =======================");

}