#include "systemLib.h"
#include <hal/efuse_hal.h>
#include "esp_app_format.h"
#include "esp_image_format.h"
#include "logLib.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_idf_version.h"
#include "esp_mac.h"
#include "esp_app_desc.h"
#include "esp_cpu.h"

#include "esp_partition.h"
#include "esp_bootloader_desc.h"

#include "esp_efuse_chip.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"

#include "esp_event.h"

#include "esp_mmu_map.h"


static const char *TAG = "system_library";

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

static const char *get_chip_id_str(esp_chip_id_t model)
{
    switch (model) {

        case ESP_CHIP_ID_ESP32:
            return "ESP32";

        case ESP_CHIP_ID_ESP32S2:
            return "ESP32-S2";

        case ESP_CHIP_ID_ESP32S3:
            return "ESP32-S3";

        case ESP_CHIP_ID_ESP32C3:
            return "ESP32-C3";

        case ESP_CHIP_ID_ESP32C2:
            return "ESP32-C2";

        case ESP_CHIP_ID_ESP32C6:
            return "ESP32-C6";

        case ESP_CHIP_ID_ESP32C61:
            return "ESP32-C61";

        case ESP_CHIP_ID_ESP32C5:
            return "ESP32-C5";

        case ESP_CHIP_ID_ESP32H2:
            return "ESP32-H2";

        case ESP_CHIP_ID_ESP32H21:
            return "ESP32-H21";

        case ESP_CHIP_ID_ESP32H4:
            return "ESP32-H4";

        case ESP_CHIP_ID_ESP32P4:
            return "ESP32-P4";

        case ESP_CHIP_ID_ESP32S31:
            return "ESP32-S3.1";

        case ESP_CHIP_ID_INVALID:
            return "Invalid chip id";

        default:
            return "Unknown ESP chip id";
    }
}

static const char *get_spi_mode_str(esp_image_spi_mode_t mode)
{
    switch (mode) {
        case ESP_IMAGE_SPI_MODE_QIO:        return "QIO";
        case ESP_IMAGE_SPI_MODE_QOUT:       return "QOUT";
        case ESP_IMAGE_SPI_MODE_DIO:        return "DIO";
        case ESP_IMAGE_SPI_MODE_DOUT:       return "DOUT";
        case ESP_IMAGE_SPI_MODE_FAST_READ:  return "FAST_READ";
        case ESP_IMAGE_SPI_MODE_SLOW_READ:  return "SLOW_READ";
        default:                            return "Unknown SPI mode";
    }
}

static const char *get_spi_speed_str(esp_image_spi_freq_t speed)
{
    switch (speed) {
        case ESP_IMAGE_SPI_SPEED_DIV_1:  return "DIV_1 (full speed)";
        case ESP_IMAGE_SPI_SPEED_DIV_2:  return "DIV_2";
        case ESP_IMAGE_SPI_SPEED_DIV_3:  return "DIV_3";
        case ESP_IMAGE_SPI_SPEED_DIV_4:  return "DIV_4";
        default:                         return "Unknown SPI speed";
    }
}

static const char *get_flash_size_str(esp_image_flash_size_t size)
{
    switch (size) {
        case ESP_IMAGE_FLASH_SIZE_1MB:   return "1 MB";
        case ESP_IMAGE_FLASH_SIZE_2MB:   return "2 MB";
        case ESP_IMAGE_FLASH_SIZE_4MB:   return "4 MB";
        case ESP_IMAGE_FLASH_SIZE_8MB:   return "8 MB";
        case ESP_IMAGE_FLASH_SIZE_16MB:  return "16 MB";
        case ESP_IMAGE_FLASH_SIZE_32MB:  return "32 MB";
        case ESP_IMAGE_FLASH_SIZE_64MB:  return "64 MB";
        case ESP_IMAGE_FLASH_SIZE_128MB: return "128 MB";
        case ESP_IMAGE_FLASH_SIZE_MAX:   return "MAX";
        default:                         return "Unknown flash size";
    }
}

static const char *get_efuse_block_str(esp_efuse_block_t block)
{
    switch (block) {
        case EFUSE_BLK0:  return "BLK0  (RESERVED)";
        case EFUSE_BLK1:  return "BLK1  (FLASH ENCRYPTION)";
        case EFUSE_BLK2:  return "BLK2  (SECURE BOOT)";
        case EFUSE_BLK3:  return "BLK3  (USER)";
        default:          return "Unknown block";
    }
}

static const char *get_coding_scheme_str(esp_efuse_coding_scheme_t scheme)
{
    switch (scheme) {
        case EFUSE_CODING_SCHEME_NONE: return "NONE";
        case EFUSE_CODING_SCHEME_3_4:   return "(3/4 coding)";
        case EFUSE_CODING_SCHEME_REPEAT:   return "(repeat coding)";
        default:                       return "Unknown coding scheme";
    }
}

static const char *get_key_purpose_str(esp_efuse_purpose_t purpose)
{
    switch (purpose) {
        case ESP_EFUSE_KEY_PURPOSE_USER:                        return "USER";
        case ESP_EFUSE_KEY_PURPOSE_SYSTEM:                    return "SYSTEM";
        case ESP_EFUSE_KEY_PURPOSE_FLASH_ENCRYPTION:           return "FLASH ENCRYPTION";
        case ESP_EFUSE_KEY_PURPOSE_SECURE_BOOT_V2:           return "SECURE BOOT";
        case ESP_EFUSE_KEY_PURPOSE_MAX:                         return "MAX";
        default:                                                return "Unknown purpose";
    }
}

void print_chip_info() {

    esp_err_t err;
    dump_t *d = NULL;

    // ====================== EFUSE REVISION ======================
    d = dump_init("EFUSE REVISION");
    uint8_t mac[6];
    efuse_hal_get_mac(mac);
    dump_add_line(d, "Factory MAC address : %02X:%02X:%02X:%02X:%02X:%02X",
         mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
    dump_add_line(d, "chip block revision : %d", efuse_hal_blk_version());
    dump_add_line(d, "flash encryption enabled : %s",
        efuse_hal_flash_encryption_enabled() ? "Yes" : "No");
    dump_add_line(d, "Skip maximum version check : %s",
        efuse_hal_get_disable_wafer_version_major() ? "Yes" : "No");
    dump_add_line(d, "Skip block version check : %s",
        efuse_hal_get_disable_blk_version_major() ? "Yes" : "No");
    dump_add_line(d, "chip revision : %d", efuse_hal_chip_revision());
    dump_add_line(d, "major chip version : %d", efuse_hal_get_major_chip_version());
    dump_add_line(d, "minor chip version : %d", efuse_hal_get_minor_chip_version());
    dump_add_line(d, "chip package version : %d", efuse_hal_get_chip_ver_pkg());
    dump_deploy(&d);

    // ========== CHIP INFO ==========
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    d = dump_init("CHIP INFO");
    dump_add_line(d, "Model        : %s", get_model_str(chip.model));
    dump_add_line(d, "Cores        : %d", chip.cores);
    dump_add_line(d, "Revision     : %d", chip.revision);
    dump_add_line(d, "WiFi 2.4GHz=%d BT Classic=%d BLE=%d",
             chip.features & CHIP_FEATURE_WIFI_BGN,
             chip.features & CHIP_FEATURE_BT,
             chip.features & CHIP_FEATURE_BLE);
    dump_add_line(d, "EMB_FLASH=%d IEEE 802.15.4=%d PSRAM=%d",
             chip.features & CHIP_FEATURE_EMB_FLASH,
             chip.features & CHIP_FEATURE_IEEE802154,
             chip.features & CHIP_FEATURE_EMB_PSRAM);
    dump_deploy(&d);

    // ========== MEMORY ==========
    d = dump_init("MEMORY");
    dump_add_line(d, "Free heap          : %u bytes", esp_get_free_heap_size());
    dump_add_line(d, "Free internal heap : %u bytes", esp_get_free_internal_heap_size());
    dump_add_line(d, "Minimum free heap  : %u bytes", esp_get_minimum_free_heap_size());
    dump_deploy(&d);

    // ========== MAC ==========
    d = dump_init("MAC");
    uint8_t base_mac[6], efuse_mac[6];
    err = esp_base_mac_addr_get(base_mac);
    if (err == ESP_OK) {
        dump_add_line(d, "Base MAC  : %02X:%02X:%02X:%02X:%02X:%02X",
            base_mac[0], base_mac[1], base_mac[2], base_mac[3], base_mac[4], base_mac[5]);
    } else {
        dump_add_line(d, "Error (%s) getting base MAC", esp_err_to_name(err));
    }
    err = esp_efuse_mac_get_default(efuse_mac);
    if (err == ESP_OK) {
        dump_add_line(d, "EFUSE MAC : %02X:%02X:%02X:%02X:%02X:%02X",
            efuse_mac[0], efuse_mac[1], efuse_mac[2], efuse_mac[3], efuse_mac[4], efuse_mac[5]);
    } else {
        dump_add_line(d, "Error (%s) getting efuse MAC", esp_err_to_name(err));
    }
    dump_deploy(&d);

    // ========== CPU ==========
    d = dump_init("CPU");
    dump_add_line(d, "Cycle cnt  : %u", esp_cpu_get_cycle_count());
    dump_add_line(d, "Stack ptr  : %p", esp_cpu_get_sp());
    dump_add_line(d, "Core ID    : %d", esp_cpu_get_core_id());
    dump_add_line(d, "Enabled IRQ mask   : 0x%X", esp_cpu_intr_get_enabled_mask());
    dump_add_line(d, "Debugger attached  : %d", esp_cpu_dbgr_is_attached());
    dump_add_line(d, "Thread ptr         : %p", esp_cpu_get_threadptr());
    dump_add_line(d, "Privilege level    : %d", esp_cpu_get_curr_privilege_level());
    dump_deploy(&d);

    // ========== APP ==========
    d = dump_init("APP");
    dump_add_line(d, "ESP-IDF version    : %s", esp_get_idf_version());
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc) {
        dump_add_line(d, "Project name  : %s", app_desc->project_name);
        dump_add_line(d, "App version   : %s", app_desc->version);
        dump_add_line(d, "IDF version   : %s", app_desc->idf_ver);
        dump_add_line(d, "Compile time  : %s %s", app_desc->date, app_desc->time);
        dump_add_line(d, "Magic word  : 0x%X (%s)", app_desc->magic_word,
            app_desc->magic_word == ESP_APP_DESC_MAGIC_WORD ? "valid" : "INVALID");
        dump_add_line(d, "Secure version  : %u", app_desc->secure_version);
        dump_add_line(d, "reserv1[0]  : 0x%X", app_desc->reserv1[0]);
        dump_add_line(d, "reserv1[1]  : 0x%X", app_desc->reserv1[1]);
        dump_add_line(d, "Minimal efuse block  : %u", app_desc->min_efuse_blk_rev_full);
        dump_add_line(d, "Maximal efuse block  : %u", app_desc->max_efuse_blk_rev_full);
        dump_add_line(d, "MMU page size (log2)  : %u", app_desc->mmu_page_size);
        dump_add_line(d, "ELF SHA256    : %s", esp_app_get_elf_sha256_str());
    } else {
        dump_add_line(d, "App description not available");
    }
    dump_deploy(&d);

    // ========== APP HEADER ==========
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
    d = dump_init("APP HEADER");
    if (partition == NULL) {
        dump_add_line(d, "Error accessing app partition");
    } else {
        void *param = malloc(sizeof(esp_image_header_t));
        err = esp_partition_read(partition, 0, param, sizeof(esp_image_header_t));
        esp_image_header_t *h = (esp_image_header_t *)param;
        if (err == ESP_OK && h != NULL) {
            dump_add_line(d, "Magic           : 0x%02X (%s)", h->magic,
                    h->magic == ESP_IMAGE_HEADER_MAGIC ? "valid" : "INVALID");
            dump_add_line(d, "Segment count   : %u",    h->segment_count);
            dump_add_line(d, "SPI mode        : %s",    get_spi_mode_str(h->spi_mode));
            dump_add_line(d, "SPI speed       : %s",    get_spi_speed_str(h->spi_speed));
            dump_add_line(d, "SPI size        : %s",    get_flash_size_str(h->spi_size));
            dump_add_line(d, "Entry addr      : 0x%08lX", h->entry_addr);
            dump_add_line(d, "WP pin          : 0x%02X (%s)", h->wp_pin,
                    h->wp_pin == 0xEE ? "disabled" : "enabled");
            dump_add_line(d, "SPI pin drv     : [%u, %u, %u]",
                    h->spi_pin_drv[0], h->spi_pin_drv[1], h->spi_pin_drv[2]);
            dump_add_line(d, "Chip ID         : %s",    get_chip_id_str(h->chip_id));
            dump_add_line(d, "Min chip rev    : %u (legacy)", h->min_chip_rev);
            dump_add_line(d, "Min chip rev    : %u.%u (full)",
                    h->min_chip_rev_full / 100, h->min_chip_rev_full % 100);
            dump_add_line(d, "Max chip rev    : %u.%u (full)",
                    h->max_chip_rev_full / 100, h->max_chip_rev_full % 100);
            dump_add_line(d, "Reserved        : [0x%02X, 0x%02X, 0x%02X, 0x%02X]",
                    h->reserved[0], h->reserved[1], h->reserved[2], h->reserved[3]);
            dump_add_line(d, "Hash appended   : %s", h->hash_appended ? "yes (SHA256)" : "no");
        } else {
            dump_add_line(d, "Error (%s) reading header partition", esp_err_to_name(err));
        }
        free(param);
    }
    dump_deploy(&d);

    // ========== BOOTLOADER DESCRIPTION ==========
    d = dump_init("BOOTLOADER");
    const esp_bootloader_desc_t *boot_desc = esp_bootloader_get_description();
    if (boot_desc != NULL) {
        dump_add_line(d, "Magic byte      : 0x%02X (%s)", boot_desc->magic_byte,
                boot_desc->magic_byte == ESP_BOOTLOADER_DESC_MAGIC_BYTE ? "valid" : "INVALID");
        dump_add_line(d, "Secure version  : %u",    boot_desc->secure_version);
        dump_add_line(d, "Version         : %lu",   boot_desc->version);
        dump_add_line(d, "IDF version     : %s",    boot_desc->idf_ver);
        dump_add_line(d, "Compile time    : %s",    boot_desc->date_time);
        dump_add_line(d, "Reserved        : [0x%02X, 0x%02X]",
                boot_desc->reserved[0], boot_desc->reserved[1]);
        dump_add_line(d, "Reserved2       : [0x%02X, 0x%02X, 0x%02X, 0x%02X, ...]",
                boot_desc->reserved2[0], boot_desc->reserved2[1],
                boot_desc->reserved2[2], boot_desc->reserved2[3]);
    } else {
        dump_add_line(d, "Error getting bootloader description");
    }
    dump_deploy(&d);

    // ========== EFUSE GLOBAL ==========
    d = dump_init("EFUSE GLOBAL");
    uint32_t sec_ver = esp_efuse_read_secure_version();
    dump_add_line(d, "Flash encryption : %s", esp_efuse_is_flash_encryption_enabled() ? "ENABLED" : "disabled");
    dump_add_line(d, "Secure version   : %u", sec_ver);
    dump_add_line(d, "Secure version valid : %s", esp_efuse_check_secure_version(sec_ver) ? "Yes" : "No");
    dump_add_line(d, "Pkg version      : %u", esp_efuse_get_pkg_ver());
    esp_err_t efuse_err = esp_efuse_check_errors();
    dump_add_line(d, "eFuse errors     : %s", efuse_err == ESP_OK ? "none" : esp_err_to_name(efuse_err));
    dump_deploy(&d);

    // ========== EFUSE BLOCKS ==========
    for (esp_efuse_block_t block = EFUSE_BLK0; block < EFUSE_BLK_MAX; block++) {
        esp_efuse_coding_scheme_t scheme = esp_efuse_get_coding_scheme(block);
        bool empty = esp_efuse_block_is_empty(block);

        char block_tag[32];
        snprintf(block_tag, sizeof(block_tag), "EFUSE %s", get_efuse_block_str(block));
        d = dump_init(block_tag);

        dump_add_line(d, "scheme: %-20s | empty: %s",
                get_coding_scheme_str(scheme), empty ? "yes" : "no");

        if (block != EFUSE_BLK0) {
            for (int reg = 0; reg < 8; reg++) {
                uint32_t val = esp_efuse_read_reg(block, reg);
                dump_add_line(d, "  [%d] 0x%08lX", reg, val);
            }
        } else {
            // Split BLK0 en 3 sous-dumps car trop grand

            // -- EFUSE BLK0 : VERSIONS --
            uint8_t chip_rev1 = 0, chip_rev2 = 0, wafer_minor = 0;
            esp_efuse_read_field_blob(ESP_EFUSE_CHIP_VER_REV1,       &chip_rev1,   esp_efuse_get_field_size(ESP_EFUSE_CHIP_VER_REV1));
            esp_efuse_read_field_blob(ESP_EFUSE_CHIP_VER_REV2,       &chip_rev2,   esp_efuse_get_field_size(ESP_EFUSE_CHIP_VER_REV2));
            esp_efuse_read_field_blob(ESP_EFUSE_WAFER_VERSION_MINOR, &wafer_minor, esp_efuse_get_field_size(ESP_EFUSE_WAFER_VERSION_MINOR));
            dump_add_line(d, "CHIP_VER_REV1        : %u (size: %d)", chip_rev1,   esp_efuse_get_field_size(ESP_EFUSE_CHIP_VER_REV1));
            dump_add_line(d, "CHIP_VER_REV2        : %u (size: %d)", chip_rev2,   esp_efuse_get_field_size(ESP_EFUSE_CHIP_VER_REV2));
            dump_add_line(d, "WAFER_VERSION_MINOR  : %u (size: %d)", wafer_minor, esp_efuse_get_field_size(ESP_EFUSE_WAFER_VERSION_MINOR));

            uint8_t chip_pkg = 0, chip_pkg_4bit = 0;
            esp_efuse_read_field_blob(ESP_EFUSE_CHIP_PACKAGE,      &chip_pkg,      esp_efuse_get_field_size(ESP_EFUSE_CHIP_PACKAGE));
            esp_efuse_read_field_blob(ESP_EFUSE_CHIP_PACKAGE_4BIT, &chip_pkg_4bit, esp_efuse_get_field_size(ESP_EFUSE_CHIP_PACKAGE_4BIT));
            dump_add_line(d, "CHIP_PACKAGE         : %u (size: %d)", chip_pkg,      esp_efuse_get_field_size(ESP_EFUSE_CHIP_PACKAGE));
            dump_add_line(d, "CHIP_PACKAGE_4BIT    : %u (size: %d)", chip_pkg_4bit, esp_efuse_get_field_size(ESP_EFUSE_CHIP_PACKAGE_4BIT));

            uint8_t mac_crc = 0;
            esp_efuse_read_field_blob(ESP_EFUSE_MAC_CRC, &mac_crc, esp_efuse_get_field_size(ESP_EFUSE_MAC_CRC));
            dump_add_line(d, "MAC_CRC              : 0x%02X (size: %d)", mac_crc, esp_efuse_get_field_size(ESP_EFUSE_MAC_CRC));

            uint8_t coding = 0;
            esp_efuse_read_field_blob(ESP_EFUSE_CODING_SCHEME, &coding, esp_efuse_get_field_size(ESP_EFUSE_CODING_SCHEME));
            dump_add_line(d, "CODING_SCHEME        : %s (size: %d)", get_coding_scheme_str(coding), esp_efuse_get_field_size(ESP_EFUSE_CODING_SCHEME));
            dump_deploy(&d);

            // -- EFUSE BLK0 : SECURITY --
            d = dump_init("EFUSE BLK0 SECURITY");
            dump_add_line(d, "JTAG_DISABLE         : %s (size: %d)", esp_efuse_read_field_bit(ESP_EFUSE_JTAG_DISABLE)            ? "YES" : "no", esp_efuse_get_field_size(ESP_EFUSE_JTAG_DISABLE));
            dump_add_line(d, "ABS_DONE_0           : %s (size: %d)", esp_efuse_read_field_bit(ESP_EFUSE_ABS_DONE_0)              ? "YES" : "no", esp_efuse_get_field_size(ESP_EFUSE_ABS_DONE_0));
            dump_add_line(d, "ABS_DONE_1           : %s (size: %d)", esp_efuse_read_field_bit(ESP_EFUSE_ABS_DONE_1)              ? "YES" : "no", esp_efuse_get_field_size(ESP_EFUSE_ABS_DONE_1));
            dump_add_line(d, "DISABLE_DL_ENCRYPT   : %s (size: %d)", esp_efuse_read_field_bit(ESP_EFUSE_DISABLE_DL_ENCRYPT)      ? "YES" : "no", esp_efuse_get_field_size(ESP_EFUSE_DISABLE_DL_ENCRYPT));
            dump_add_line(d, "DISABLE_DL_DECRYPT   : %s (size: %d)", esp_efuse_read_field_bit(ESP_EFUSE_DISABLE_DL_DECRYPT)      ? "YES" : "no", esp_efuse_get_field_size(ESP_EFUSE_DISABLE_DL_DECRYPT));
            dump_add_line(d, "DISABLE_DL_CACHE     : %s (size: %d)", esp_efuse_read_field_bit(ESP_EFUSE_DISABLE_DL_CACHE)        ? "YES" : "no", esp_efuse_get_field_size(ESP_EFUSE_DISABLE_DL_CACHE));
            dump_add_line(d, "UART_DOWNLOAD_DIS    : %s (size: %d)", esp_efuse_read_field_bit(ESP_EFUSE_UART_DOWNLOAD_DIS)       ? "YES" : "no", esp_efuse_get_field_size(ESP_EFUSE_UART_DOWNLOAD_DIS));
            dump_add_line(d, "KEY_STATUS           : %s (size: %d)", esp_efuse_read_field_bit(ESP_EFUSE_KEY_STATUS)              ? "YES" : "no", esp_efuse_get_field_size(ESP_EFUSE_KEY_STATUS));
            dump_add_line(d, "CONSOLE_DEBUG_DIS    : %s (size: %d)", esp_efuse_read_field_bit(ESP_EFUSE_CONSOLE_DEBUG_DISABLE)   ? "YES" : "no", esp_efuse_get_field_size(ESP_EFUSE_CONSOLE_DEBUG_DISABLE));
            dump_add_line(d, "DISABLE_SDIO_HOST    : %s (size: %d)", esp_efuse_read_field_bit(ESP_EFUSE_DISABLE_SDIO_HOST)       ? "YES" : "no", esp_efuse_get_field_size(ESP_EFUSE_DISABLE_SDIO_HOST));
            dump_add_line(d, "DISABLE_APP_CPU      : %s (size: %d)", esp_efuse_read_field_bit(ESP_EFUSE_DISABLE_APP_CPU)         ? "YES" : "no", esp_efuse_get_field_size(ESP_EFUSE_DISABLE_APP_CPU));
            dump_add_line(d, "DISABLE_BT           : %s (size: %d)", esp_efuse_read_field_bit(ESP_EFUSE_DISABLE_BT)              ? "YES" : "no", esp_efuse_get_field_size(ESP_EFUSE_DISABLE_BT));
            dump_add_line(d, "DIS_CACHE            : %s (size: %d)", esp_efuse_read_field_bit(ESP_EFUSE_DIS_CACHE)               ? "YES" : "no", esp_efuse_get_field_size(ESP_EFUSE_DIS_CACHE));
            dump_add_line(d, "BLK3_PART_RESERVE    : %s (size: %d)", esp_efuse_read_field_bit(ESP_EFUSE_BLK3_PART_RESERVE)       ? "YES" : "no", esp_efuse_get_field_size(ESP_EFUSE_BLK3_PART_RESERVE));
            dump_add_line(d, "CHIP_CPU_FREQ_RATED  : %s (size: %d)", esp_efuse_read_field_bit(ESP_EFUSE_CHIP_CPU_FREQ_RATED)     ? "YES" : "no", esp_efuse_get_field_size(ESP_EFUSE_CHIP_CPU_FREQ_RATED));
            dump_add_line(d, "CHIP_CPU_FREQ_LOW    : %s (size: %d)", esp_efuse_read_field_bit(ESP_EFUSE_CHIP_CPU_FREQ_LOW)       ? "YES" : "no", esp_efuse_get_field_size(ESP_EFUSE_CHIP_CPU_FREQ_LOW));
            dump_deploy(&d);

            // -- EFUSE BLK0 : FLASH / CLOCK / SPI --
            d = dump_init("EFUSE BLK0 FLASH SPI");
            uint8_t wr_dis = 0, rd_dis = 0;
            esp_efuse_read_field_blob(ESP_EFUSE_WR_DIS, &wr_dis, esp_efuse_get_field_size(ESP_EFUSE_WR_DIS));
            esp_efuse_read_field_blob(ESP_EFUSE_RD_DIS, &rd_dis, esp_efuse_get_field_size(ESP_EFUSE_RD_DIS));
            dump_add_line(d, "WR_DIS               : 0x%04X (size: %d)", wr_dis, esp_efuse_get_field_size(ESP_EFUSE_WR_DIS));
            dump_add_line(d, "RD_DIS               : 0x%01X  (size: %d)", rd_dis, esp_efuse_get_field_size(ESP_EFUSE_RD_DIS));

            uint8_t flash_crypt_cnt = 0, flash_crypt_cfg = 0;
            esp_efuse_read_field_blob(ESP_EFUSE_FLASH_CRYPT_CNT,    &flash_crypt_cnt, esp_efuse_get_field_size(ESP_EFUSE_FLASH_CRYPT_CNT));
            esp_efuse_read_field_blob(ESP_EFUSE_FLASH_CRYPT_CONFIG, &flash_crypt_cfg, esp_efuse_get_field_size(ESP_EFUSE_FLASH_CRYPT_CONFIG));
            dump_add_line(d, "FLASH_CRYPT_CNT      : %u (%s) (size: %d)", flash_crypt_cnt,
                    (__builtin_popcount(flash_crypt_cnt) % 2 == 1) ? "encryption ON" : "encryption OFF",
                    esp_efuse_get_field_size(ESP_EFUSE_FLASH_CRYPT_CNT));
            dump_add_line(d, "FLASH_CRYPT_CONFIG   : 0x%X (size: %d)", flash_crypt_cfg, esp_efuse_get_field_size(ESP_EFUSE_FLASH_CRYPT_CONFIG));

            uint8_t clk8m = 0, adc_vref = 0, vol_level = 0;
            esp_efuse_read_field_blob(ESP_EFUSE_CLK8M_FREQ,       &clk8m,     esp_efuse_get_field_size(ESP_EFUSE_CLK8M_FREQ));
            esp_efuse_read_field_blob(ESP_EFUSE_ADC_VREF,         &adc_vref,  esp_efuse_get_field_size(ESP_EFUSE_ADC_VREF));
            esp_efuse_read_field_blob(ESP_EFUSE_VOL_LEVEL_HP_INV, &vol_level, esp_efuse_get_field_size(ESP_EFUSE_VOL_LEVEL_HP_INV));
            dump_add_line(d, "CLK8M_FREQ           : %u (size: %d)", clk8m,     esp_efuse_get_field_size(ESP_EFUSE_CLK8M_FREQ));
            dump_add_line(d, "ADC_VREF             : %u (size: %d)", adc_vref,  esp_efuse_get_field_size(ESP_EFUSE_ADC_VREF));
            dump_add_line(d, "VOL_LEVEL_HP_INV     : %u (size: %d)", vol_level, esp_efuse_get_field_size(ESP_EFUSE_VOL_LEVEL_HP_INV));

            dump_add_line(d, "XPD_SDIO_REG         : %s (size: %d)", esp_efuse_read_field_bit(ESP_EFUSE_XPD_SDIO_REG)   ? "YES" : "no",    esp_efuse_get_field_size(ESP_EFUSE_XPD_SDIO_REG));
            dump_add_line(d, "XPD_SDIO_TIEH        : %s (size: %d)", esp_efuse_read_field_bit(ESP_EFUSE_XPD_SDIO_TIEH)  ? "3.3V" : "1.8V", esp_efuse_get_field_size(ESP_EFUSE_XPD_SDIO_TIEH));
            dump_add_line(d, "XPD_SDIO_FORCE       : %s (size: %d)", esp_efuse_read_field_bit(ESP_EFUSE_XPD_SDIO_FORCE) ? "YES" : "no",    esp_efuse_get_field_size(ESP_EFUSE_XPD_SDIO_FORCE));

            uint8_t spi_clk=0, spi_q=0, spi_d=0, spi_cs0=0, spi_hd=0;
            esp_efuse_read_field_blob(ESP_EFUSE_SPI_PAD_CONFIG_CLK, &spi_clk,  esp_efuse_get_field_size(ESP_EFUSE_SPI_PAD_CONFIG_CLK));
            esp_efuse_read_field_blob(ESP_EFUSE_SPI_PAD_CONFIG_Q,   &spi_q,    esp_efuse_get_field_size(ESP_EFUSE_SPI_PAD_CONFIG_Q));
            esp_efuse_read_field_blob(ESP_EFUSE_SPI_PAD_CONFIG_D,   &spi_d,    esp_efuse_get_field_size(ESP_EFUSE_SPI_PAD_CONFIG_D));
            esp_efuse_read_field_blob(ESP_EFUSE_SPI_PAD_CONFIG_CS0, &spi_cs0,  esp_efuse_get_field_size(ESP_EFUSE_SPI_PAD_CONFIG_CS0));
            esp_efuse_read_field_blob(ESP_EFUSE_SPI_PAD_CONFIG_HD,  &spi_hd,   esp_efuse_get_field_size(ESP_EFUSE_SPI_PAD_CONFIG_HD));
            dump_add_line(d, "SPI_PAD_CLK          : %u (size: %d)", spi_clk,  esp_efuse_get_field_size(ESP_EFUSE_SPI_PAD_CONFIG_CLK));
            dump_add_line(d, "SPI_PAD_Q            : %u (size: %d)", spi_q,    esp_efuse_get_field_size(ESP_EFUSE_SPI_PAD_CONFIG_Q));
            dump_add_line(d, "SPI_PAD_D            : %u (size: %d)", spi_d,    esp_efuse_get_field_size(ESP_EFUSE_SPI_PAD_CONFIG_D));
            dump_add_line(d, "SPI_PAD_CS0          : %u (size: %d)", spi_cs0,  esp_efuse_get_field_size(ESP_EFUSE_SPI_PAD_CONFIG_CS0));
            dump_add_line(d, "SPI_PAD_HD           : %u (size: %d)", spi_hd,   esp_efuse_get_field_size(ESP_EFUSE_SPI_PAD_CONFIG_HD));
            dump_deploy(&d);

            // on remet d à NULL pour que le dump_deploy(&d) du bloc parent ne double-free pas
            d = NULL;
        }

        if (d != NULL) dump_deploy(&d);
    }

    // ========== EFUSE KEY BLOCKS ==========
    for (esp_efuse_block_t key = EFUSE_BLK_KEY0; key < EFUSE_BLK_KEY_MAX; key++) {
        char key_tag[32];
        snprintf(key_tag, sizeof(key_tag), "EFUSE KEY BLK%d", key);
        d = dump_init(key_tag);

        bool rd_dis      = esp_efuse_get_key_dis_read(key);
        bool wr_dis      = esp_efuse_get_key_dis_write(key);
        bool unused      = esp_efuse_key_block_unused(key);
        bool purpose_dis = esp_efuse_get_keypurpose_dis_write(key);
        esp_efuse_purpose_t purpose = esp_efuse_get_key_purpose(key);
        esp_efuse_block_t found_block;
        bool found = esp_efuse_find_purpose(purpose, &found_block);

        dump_add_line(d, "purpose         : %s", get_key_purpose_str(purpose));
        dump_add_line(d, "purpose find    : %s (BLK%d)", found ? "found" : "not found", found_block);
        dump_add_line(d, "unused          : %s", unused      ? "yes" : "no");
        dump_add_line(d, "read  protect   : %s", rd_dis      ? "YES" : "no");
        dump_add_line(d, "write protect   : %s", wr_dis      ? "YES" : "no");
        dump_add_line(d, "purpose wr dis  : %s", purpose_dis ? "YES" : "no");
        dump_deploy(&d);
    }

    // ========== EVENT LOOP ==========
    char *buf = (char*)malloc(10001);
    if (buf == NULL) {
        log_msg(TAG, "malloc failed (event loop)");
    } else {
        memset(buf, 0, 10001);
        FILE *f = fmemopen(buf, 10000, "w");
        if (f == NULL) {
            log_msg(TAG, "fmemopen failed (event loop)");
        } else {
            err = esp_event_dump(f);
            fflush(f);
            fclose(f);
            if (err != ESP_OK) {
                log_msg(TAG, "Error (%s) event loop dump", esp_err_to_name(err));
            } else {
                d = dump_init("EVENT LOOP");
                char *ptr = buf;
                char *end = buf + strlen(buf);
                while (ptr < end) {
                    char *newline = strchr(ptr, '\n');
                    int len = newline ? (int)(newline - ptr) : (int)(end - ptr);
                    if (len > 0) dump_add_line(d, "%.*s", len, ptr);
                    ptr += len + (newline ? 1 : 0);
                }
                dump_deploy(&d);
            }
        }
        free(buf);
    }

    // ========== MMU MAPPED BLOCKS ==========
    char *buf_mmu = (char*)malloc(10001);
    if (buf_mmu == NULL) {
        log_msg(TAG, "malloc failed (mmu)");
    } else {
        memset(buf_mmu, 0, 10001);
        FILE *f = fmemopen(buf_mmu, 10000, "w");
        if (f == NULL) {
            log_msg(TAG, "fmemopen failed (mmu)");
        } else {
            err = esp_mmu_map_dump_mapped_blocks(f);
            fflush(f);
            fclose(f);
            if (err != ESP_OK) {
                log_msg(TAG, "Error (%s) mmu mapped blocks dump", esp_err_to_name(err));
            } else {
                d = dump_init("MMU BLOCKS");
                char *ptr = buf_mmu;
                char *end = buf_mmu + strlen(buf_mmu);
                while (ptr < end) {
                    char *newline = strchr(ptr, '\n');
                    int len = newline ? (int)(newline - ptr) : (int)(end - ptr);
                    if (len > 0) dump_add_line(d, "%.*s", len, ptr);
                    ptr += len + (newline ? 1 : 0);
                }
                dump_deploy(&d);
            }
        }
        free(buf_mmu);
    }
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