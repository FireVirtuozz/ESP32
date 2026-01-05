#include "efuseLib.h"
#include <hal/efuse_hal.h>
#include "mqttLib.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_idf_version.h"
#include "esp_mac.h"
#include "esp_app_desc.h"
#include "esp_cpu.h"

static const char *TAG = "efuse_library";

static const char *get_model_str(esp_chip_model_t model)
{
    switch (model) {

        case CHIP_ESP32:
            return "ESP32";

        case CHIP_ESP32S2:
            return "ESP32-S2";

        case CHIP_ESP32S3:
            return "ESP32-S3";

        case CHIP_ESP32C3:
            return "ESP32-C3";

        case CHIP_ESP32C2:
            return "ESP32-C2";

        case CHIP_ESP32C6:
            return "ESP32-C6";

        case CHIP_ESP32C61:
            return "ESP32-C61";

        case CHIP_ESP32C5:
            return "ESP32-C5";

        case CHIP_ESP32H2:
            return "ESP32-H2";

        case CHIP_ESP32H21:
            return "ESP32-H21";

        case CHIP_ESP32H4:
            return "ESP32-H4";

        case CHIP_ESP32P4:
            return "ESP32-P4";

        case CHIP_ESP32S31:
            return "ESP32-S3.1";

        case CHIP_POSIX_LINUX:
            return "POSIX/Linux simulator";

        default:
            return "Unknown ESP chip";
    }
}

void print_chip_info() {

    log_mqtt(LOG_INFO, TAG, true, "====================== EFUSE info ======================");

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

    log_mqtt(LOG_INFO, TAG, true, "===================== EFUSE info end =======================");

    esp_chip_info_t chip;
    esp_chip_info(&chip);

    log_mqtt(LOG_INFO, TAG, true, "========== CHIP INFO ==========");
    log_mqtt(LOG_INFO, TAG, true, "Model        : %s", get_model_str(chip.model));
    log_mqtt(LOG_INFO, TAG, true, "Cores        : %d", chip.cores);
    log_mqtt(LOG_INFO, TAG, true, "Revision     : %d", chip.revision);
    log_mqtt(LOG_INFO, TAG, true, "Features[0]  : WiFi 2.4GHz=%d BT Classic=%d BLE=%d",
             chip.features & CHIP_FEATURE_WIFI_BGN,
             chip.features & CHIP_FEATURE_BT,
             chip.features & CHIP_FEATURE_BLE);
    log_mqtt(LOG_INFO, TAG, true, "Features[1]  : EMB_FLASH=%d IEEE 802.15.4=%d PSRAM=%d",
             chip.features & CHIP_FEATURE_EMB_FLASH,
             chip.features & CHIP_FEATURE_IEEE802154,
             chip.features & CHIP_FEATURE_EMB_PSRAM);
    log_mqtt(LOG_INFO, TAG, true, "================================");


    log_mqtt(LOG_INFO, TAG, true, "========== SYSTEM ==========");
    log_mqtt(LOG_INFO, TAG, true, "Free heap          : %u bytes", esp_get_free_heap_size());
    log_mqtt(LOG_INFO, TAG, true, "Free internal heap : %u bytes", esp_get_free_internal_heap_size());
    log_mqtt(LOG_INFO, TAG, true, "Minimum free heap  : %u bytes", esp_get_minimum_free_heap_size());
    log_mqtt(LOG_INFO, TAG, true, "================================");

    log_mqtt(LOG_INFO, TAG, true, "ESP-IDF version    : %s", esp_get_idf_version());

    log_mqtt(LOG_INFO, TAG, true, "========== MAC ==========");
    uint8_t base_mac[6], efuse_mac[6];
    esp_base_mac_addr_get(base_mac);
    esp_efuse_mac_get_default(efuse_mac);
    log_mqtt(LOG_INFO, TAG, true, "Base MAC  : %02X:%02X:%02X:%02X:%02X:%02X",
            base_mac[0], base_mac[1], base_mac[2], base_mac[3], base_mac[4], base_mac[5]);
    log_mqtt(LOG_INFO, TAG, true, "EFUSE MAC : %02X:%02X:%02X:%02X:%02X:%02X",
            efuse_mac[0], efuse_mac[1], efuse_mac[2], efuse_mac[3], efuse_mac[4], efuse_mac[5]);
    log_mqtt(LOG_INFO, TAG, true, "================================");

    log_mqtt(LOG_INFO, TAG, true, "========== CPU ==========");
    log_mqtt(LOG_INFO, TAG, true, "Cycle cnt  : %u", esp_cpu_get_cycle_count());
    log_mqtt(LOG_INFO, TAG, true, "Stack ptr  : %p", esp_cpu_get_sp());
    log_mqtt(LOG_INFO, TAG, true, "Core ID    : %d", esp_cpu_get_core_id());
    log_mqtt(LOG_INFO, TAG, true, "Enabled IRQ mask   : 0x%X", esp_cpu_intr_get_enabled_mask());
    log_mqtt(LOG_INFO, TAG, true, "Debugger attached  : %d", esp_cpu_dbgr_is_attached());
    log_mqtt(LOG_INFO, TAG, true, "Thread ptr         : %p", esp_cpu_get_threadptr());
    log_mqtt(LOG_INFO, TAG, true, "Privilege level    : %d", esp_cpu_get_curr_privilege_level());
    /*
    esp_cpu_intr_get_desc();
    esp_cpu_intr_has_handler();
    esp_cpu_intr_get_handler_arg();
    */
    log_mqtt(LOG_INFO, TAG, true, "================================");


    log_mqtt(LOG_INFO, TAG, true, "========== APP ==========");
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc) {
        log_mqtt(LOG_INFO, TAG, true, "Project name  : %s", app_desc->project_name);
        log_mqtt(LOG_INFO, TAG, true, "App version   : %s", app_desc->version);
        log_mqtt(LOG_INFO, TAG, true, "IDF version   : %s", app_desc->idf_ver);
        log_mqtt(LOG_INFO, TAG, true, "Compile time  : %s %s", app_desc->date, app_desc->time);
        log_mqtt(LOG_INFO, TAG, true, "Magic word  : 0x%X", app_desc->magic_word);
        log_mqtt(LOG_INFO, TAG, true, "Secure version  : %u", app_desc->secure_version);
        log_mqtt(LOG_INFO, TAG, true, "reserv1[0]  : 0x%X", app_desc->reserv1[0]);
        log_mqtt(LOG_INFO, TAG, true, "reserv1[1]  : 0x%X", app_desc->reserv1[1]);
        log_mqtt(LOG_INFO, TAG, true, "Minimal efuse block  : %u", app_desc->min_efuse_blk_rev_full);
        log_mqtt(LOG_INFO, TAG, true, "Maximal efuse block  : %u", app_desc->max_efuse_blk_rev_full);
        log_mqtt(LOG_INFO, TAG, true, "MMU page size (log2)  : %u", app_desc->mmu_page_size);
        //reserv3
        //reserv2
        //app_elf_sha256
        log_mqtt(LOG_INFO, TAG, true, "ELF SHA256    : %s", esp_app_get_elf_sha256_str());
    } else {
        log_mqtt(LOG_WARN, TAG, true, "App description not available");
    }
    log_mqtt(LOG_INFO, TAG, true, "================================");

}

void restart_chip() {
    esp_restart();
}

/*system*/
//esp_register_shutdown_handler
//esp_unregister_shutdown_handler

//esp_reset_reason
//esp_system_abort

/*mac*/
//esp_base_mac_addr_set
//esp_efuse_mac_get_custom
//esp_read_mac
//esp_derive_local_mac
//esp_iface_mac_addr_set
//esp_mac_addr_len_get

/*cpu*/
//esp_cpu_stall
//esp_cpu_unstall
//esp_cpu_reset
//esp_cpu_wait_for_intr
//esp_cpu_set_cycle_count
//esp_cpu_pc_to_addr
//esp_cpu_set_threadptr
//esp_cpu_intr_set_handler
//esp_cpu_intr_enable
//esp_cpu_intr_disable
//esp_cpu_intr_edge_ack
//esp_cpu_configure_region_protection
//esp_cpu_set_breakpoint
//esp_cpu_clear_breakpoint
//esp_cpu_set_watchpoint
//esp_cpu_clear_watchpoint
//esp_cpu_dbgr_break
//esp_cpu_get_call_addr
//esp_cpu_compare_and_set