#include "vl53l1x.h"
#include "sensors_lib.h"
#include "peripherals/i2c_helper.h"
#include "log_lib.h"
#include <string.h>

#if CONFIG_USE_UDPLIB
#include "udp_lib.h"
#endif

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "vl53l1x_sensor";

#if CONFIG_USE_VL53L1X

// --- Registers confirmed directly from ST datasheet DS12385 + UM2356 ---
#define REG_IDENTIFICATION_MODEL_ID 0x010F // expect 0xEACC (model 0xEA, module 0xCC)
#define REG_FIRMWARE_SYSTEM_STATUS  0x00E5 // bit0: firmware boot complete
#define REG_SYSTEM_MODE_START       0x0087 // write 0x40: start ranging (continuous)
#define REG_SYSTEM_INTERRUPT_CLEAR  0x0086 // write 0x01: clear interrupt / consume result
#define REG_RESULT_RANGE_STATUS     0x0089
#define REG_RESULT_FINAL_DISTANCE   0x0096
#define REG_CONFIG_TABLE_START      0x002D // where the 91-byte default config table loads

#define REG_VHV_CONFIG_TIMEOUT_MACROP_LOOP_BOUND 0x000B
#define REG_VHV_CONFIG_COUNT_MAX_SUBBANK         0x000C

// --- Registers used for the "is a measurement ready" check ---
// UM2356 abstracts this behind VL53L1X_CheckForDataReady() and does not
// document the raw bit logic; the pattern below (read GPIO_HV_MUX__CTRL's
// bit4 as the interrupt polarity, then compare it against
// GPIO__TIO_HV_STATUS's bit0) is not from an ST document but is the
// well-established community reference implementation (Pololu's VL53L1X
// driver and its many derivatives), independently confirmed by several
// third-party register-map dumps.
#define REG_GPIO_HV_MUX_CTRL 0x0030
#define REG_GPIO_TIO_HV_STATUS 0x0031

static i2c_master_dev_handle_t dev;

static esp_err_t wait_for_boot(void) {
    for (int retry = 0; retry < 50; retry++) {
        uint8_t boot_state = 0;
        esp_err_t err = i2c_bus_read_reg16addr(dev, REG_FIRMWARE_SYSTEM_STATUS, &boot_state, 1);
        if (err == ESP_OK && (boot_state & 0x01)) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t check_model_id(void) {
    uint8_t raw[2];
    esp_err_t err = i2c_bus_read_reg16addr(dev, REG_IDENTIFICATION_MODEL_ID, raw, sizeof(raw));
    if (err != ESP_OK) {
        return err;
    }
    uint16_t id = ((uint16_t)raw[0] << 8) | raw[1];
    if (id != 0xEACC) {
        log_msg(TAG, "Unexpected model ID 0x%04X (expected 0xEACC)", id);
        return ESP_ERR_INVALID_VERSION;
    }
    return ESP_OK;
}

static esp_err_t reset_vhv_config(void) {
    esp_err_t err;

    // Forcer le timeout de boucle MacroP VHV (valeur référence ST = 0x09)
    err = i2c_bus_write_reg16addr(dev, REG_VHV_CONFIG_TIMEOUT_MACROP_LOOP_BOUND, 0x09);
    if (err != ESP_OK) return err;

    // Réinitialiser le compteur de subbank VHV
    err = i2c_bus_write_reg16addr(dev, REG_VHV_CONFIG_COUNT_MAX_SUBBANK, 0x00);
    if (err != ESP_OK) return err;

    return ESP_OK;
}

// Configuration de la ROI (Zone d'intérêt) - Registres ST 0x004F à 0x0052
static esp_err_t set_roi(uint8_t width, uint8_t height, uint8_t center_spad) {
    if (width < 4) width = 4;
    if (height < 4) height = 4;
    if (width > 16) width = 16;
    if (height > 16) height = 16;

    uint8_t reg_val = ((height - 1) << 4) | (width - 1);
    esp_err_t err = i2c_bus_write_reg16addr(dev, 0x004F, reg_val); // ROI config
    if (err != ESP_OK) return err;

    return i2c_bus_write_reg16addr(dev, 0x0050, center_spad); // Center SPAD
}

// Exemple pour configurer le mode SHORT (registres issus du driver officiel ST / Pololu)
static esp_err_t set_distance_mode_short(void) {
    esp_err_t err;

    // 1. PHASECAL & VCSEL Periods
    err = i2c_bus_write_reg16addr(dev, 0x004B, 0x09); if (err != ESP_OK) return err;
    err = i2c_bus_write_reg16addr(dev, 0x0060, 0x07); if (err != ESP_OK) return err;
    err = i2c_bus_write_reg16addr(dev, 0x0063, 0x05); if (err != ESP_OK) return err;
    err = i2c_bus_write_reg16addr(dev, 0x0069, 0x38); if (err != ESP_OK) return err;

    // 2. WOI & Initial Phase (Ajustement du déphasage DSP)
    uint8_t woi_config[]    = { 0x07, 0x05 };
    uint8_t initial_phase[] = { 0x06, 0x06 };
    err = i2c_bus_write_block16addr(dev, 0x0078, woi_config, 2); if (err != ESP_OK) return err;
    err = i2c_bus_write_block16addr(dev, 0x007A, initial_phase, 2); if (err != ESP_OK) return err;

    // 3. Timeouts MacroPeriod A & B
    uint8_t timeout_macrop_a[] = { 0x00, 0x1D };
    uint8_t timeout_macrop_b[] = { 0x00, 0x27 };
    err = i2c_bus_write_block16addr(dev, 0x005E, timeout_macrop_a, 2); if (err != ESP_OK) return err;
    err = i2c_bus_write_block16addr(dev, 0x0061, timeout_macrop_b, 2); if (err != ESP_OK) return err;

    // 4. Réglage des targets DSS pour éviter la saturation à courte distance
    err = i2c_bus_write_reg16addr(dev, 0x005B, 0x00); if (err != ESP_OK) return err; // SD_CONFIG__WOI_TARGET_SETPOINT
    err = i2c_bus_write_reg16addr(dev, 0x005C, 0x08); if (err != ESP_OK) return err;
    err = i2c_bus_write_reg16addr(dev, 0x005D, 0x00); if (err != ESP_OK) return err;

    return ESP_OK;
}

// 91-byte factory default configuration table, loaded starting at 0x002D.
// Matches ST's official VL51L1X_DEFAULT_CONFIGURATION array size and content.
static const uint8_t DEFAULT_CONFIG[] = {
    0x12, 0x00, 0x00, 0x11, 0x02, 0x00, 0x02, 0x08, 0x00, 0x08, 0x10, 0x01, 0x01, 0x00, 0x00, 0x00,
    0x00, 0xff, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x0b, 0x00, 0x00, 0x02, 0x0a, 0x21,
    0x00, 0x00, 0x05, 0x05, 0x15, 0x05, 0x03, 0x08, 0x03, 0x08, 0x35, 0x00, 0x03, 0x04, 0x03, 0x08,
    0x01, 0x01, 0x01, 0x00, 0x01, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x20, 0x0b, 0x00, 0x00, 0x02, 0x0a,
    0x00, 0x00, 0x03, 0x01, 0x00, 0x02, 0x01, 0x01, 0x00, 0x01, 0x0c, 0x08, 0x01, 0x00, 0x00, 0x08,
    0x01, 0x14, 0x00, 0x00, 0x00, 0x07, 0x03, 0x05, 0x05, 0x15, 0x03
};

static esp_err_t load_default_config(void) {
    return i2c_bus_write_block16addr(dev, REG_CONFIG_TABLE_START, DEFAULT_CONFIG, sizeof(DEFAULT_CONFIG));
}

/**
 * Whether a new ranging result is available.
 * See the REG_GPIO_HV_MUX_CTRL / REG_GPIO_TIO_HV_STATUS comment above:
 * community reference pattern, not directly documented by ST's public PDFs.
 */
static esp_err_t data_ready(bool *ready) {
    uint8_t mux_ctrl = 0, status = 0;
    esp_err_t err = i2c_bus_read_reg16addr(dev, REG_GPIO_HV_MUX_CTRL, &mux_ctrl, 1);
    if (err != ESP_OK) { log_msg(TAG, "I2C error reading mux_ctrl: %s", esp_err_to_name(err)); return err; }
    err = i2c_bus_read_reg16addr(dev, REG_GPIO_TIO_HV_STATUS, &status, 1);
    if (err != ESP_OK) { log_msg(TAG, "I2C error reading status: %s", esp_err_to_name(err)); return err; }

    bool expected_polarity = !((mux_ctrl & 0x10) >> 4);
    *ready = ((status & 0x01) == expected_polarity);
    //log_msg(TAG, "mux_ctrl=0x%02X status=0x%02X expected_pol=%d ready=%d", mux_ctrl, status, expected_polarity, *ready);
    return ESP_OK;
}

// Table de conversion officielle du registre 0x0089 (Driver ST ULD)
static const uint8_t ST_STATUS_LOOKUP[] = {
    255, 255, 255, 5, 2, 4, 1, 7, 3, 0, 255, 255, 8, 13, 255, 255,
    255, 255, 10, 6, 255, 255, 11, 12
};

static void vl53l1x_task(void *params) {
    (void)params;

    // XSHUT sequence (DS12385 3.6, Option 1): hold low, then release high
    // to boot the sensor, before touching it over I2C.
    gpio_reset_pin(VL53L1X_XSHUT_GPIO);
    gpio_set_direction(VL53L1X_XSHUT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(VL53L1X_XSHUT_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(VL53L1X_XSHUT_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    if (i2c_bus_add_device(VL53L1X_I2C_ADDR, 400000, &dev) != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    if (wait_for_boot() != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Firmware boot timeout");
        vTaskDelete(NULL);
        return;
    }

    if (check_model_id() != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Model ID check failed, check wiring/XSHUT");
        vTaskDelete(NULL);
        return;
    }

    if (load_default_config() != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error loading default configuration table");
        vTaskDelete(NULL);
        return;
    }

    // Réarmement explicite du VHV pour les boots à chaud
    if (reset_vhv_config() != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error resetting VHV configuration");
        vTaskDelete(NULL);
        return;
    }


    if (set_distance_mode_short() != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error setting distance mode");
        vTaskDelete(NULL);
        return;
    }

    if (set_roi(8, 8, 199) != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error setting distance mode");
        vTaskDelete(NULL);
        return;
    }

    // Timing budget intentionally NOT set here. ST's real
    // SetMeasurementTimingBudgetMicroSeconds() encodes the value through a
    // macro-period calculation that depends on the currently configured
    // VCSEL period (UM2356 section 2.4) — it is not a raw millisecond
    // value written directly to a register. Writing an un-encoded value
    // there produces an undefined, not a 50ms, budget. The default
    // configuration table already sets a valid working timing mode; if you
    // need a specific budget, port ST's SetMeasurementTimingBudget logic
    // rather than hand-writing these registers.

    esp_err_t err;

    uint8_t osc_buf[2];
    err= i2c_bus_read_reg16addr(dev, 0x00DE, osc_buf, 2);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed to set inter-measurement period");
        vTaskDelete(NULL);
        return;
    }
    uint16_t clock_pll = ((uint16_t)osc_buf[0] << 8 | osc_buf[1]) & 0x03FF;

    // Calcul précis ST : ClockPLL * InterMeasurementMs * 1.075
    uint32_t inter_measurement_ms = 100; // Utiliser au moins 100ms pour éviter le dépassement du Timing Budget
    uint32_t clock_cycles = (uint32_t)(clock_pll * inter_measurement_ms * 1.075f);

    uint8_t inter_meas_buf[4] = {
        (uint8_t)(clock_cycles >> 24),
        (uint8_t)(clock_cycles >> 16),
        (uint8_t)(clock_cycles >> 8),
        (uint8_t)(clock_cycles & 0xFF)
    };

    // Registre SYSTEM__INTERMEASUREMENT_PERIOD = 0x006C
    err = i2c_bus_write_block16addr(dev, 0x006C, inter_meas_buf, 4);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed to set inter-measurement period");
        vTaskDelete(NULL);
        return;
    }

    err = i2c_bus_write_reg16addr(dev, REG_SYSTEM_MODE_START, 0x40); // start continuous ranging
    if (err != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }


    err = i2c_bus_write_reg16addr(dev, REG_SYSTEM_INTERRUPT_CLEAR, 0x01); // kick off the first measurement
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Interrupt clear FAILED: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    log_msg(TAG, "VL53L1X initialized at address 0x%02X (XSHUT: GPIO%d)", VL53L1X_I2C_ADDR, VL53L1X_XSHUT_GPIO);

    uint8_t ready_timeout_count = 0;

    while (true) {
        bool ready = false;
        if (data_ready(&ready) == ESP_OK && ready) 
        {
            ready_timeout_count = 0; // Réinitialise le compteur sur succès

            uint8_t status_raw = 0;
            err = i2c_bus_read_reg16addr(dev, REG_RESULT_RANGE_STATUS, &status_raw, 1);
            if (err != ESP_OK) {
                log_msg_lvl(ESP_LOG_ERROR, TAG, "Range status reading failed: %s", esp_err_to_name(err));
                continue;
            }

            uint8_t dist_raw[2];
            err = i2c_bus_read_reg16addr(dev, REG_RESULT_FINAL_DISTANCE, dist_raw, sizeof(dist_raw));
            if (err != ESP_OK) {
                log_msg_lvl(ESP_LOG_ERROR, TAG, "Distance reading failed: %s", esp_err_to_name(err));
                continue;
            }
            uint16_t distance_mm = (uint16_t)((dist_raw[0] << 8) | dist_raw[1]);

            // Toujours effacer l'interruption pour permettre la mesure suivante
            err = i2c_bus_write_reg16addr(dev, REG_SYSTEM_INTERRUPT_CLEAR, 0x01);
            if (err != ESP_OK) {
                log_msg_lvl(ESP_LOG_ERROR, TAG, "Interrupt clear FAILED: %s", esp_err_to_name(err));
                continue;
            }

            uint8_t raw_code = status_raw & 0x1F;
            uint8_t range_status = (raw_code < sizeof(ST_STATUS_LOOKUP)) ? ST_STATUS_LOOKUP[raw_code] : 255;

            switch (range_status) { // 0 = mesure valide (ST ULD API)
                case 0:
                    header_sensor_t header = {0};
                    header.esp_id = (uint8_t)CONFIG_ESP_ID;
                    header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
                    header.type = SENSOR_TYPE_VL53L1X;
                    uint8_t buf[HEADER_SENSOR_SIZE + sizeof(uint16_t)];
                    serialize_header(&header, buf);
                    memcpy(&buf[HEADER_SENSOR_SIZE], &distance_mm, sizeof(uint16_t));
                    log_msg(TAG, "Distance: %u mm", distance_mm);

    #if CONFIG_USE_UDPLIB
                    send_udp_sensor(buf, sizeof(buf));
    #endif
                    break;
                case 2:
                    break;
                default:
                    log_msg(TAG, "Invalid range, raw distance %u mm, status %u (ST code %u)", 
                        distance_mm, status_raw, range_status);
                    break;
            }
        } 
        else 
        {
            // Sécurité : Si ready reste à false pendant 5 cycles (1 seconde)
            ready_timeout_count++;
            if (ready_timeout_count >= 100) {
                log_msg_lvl(ESP_LOG_WARN, TAG, "Sensor hung, resetting ranging sequence...");
                
                // 1. Stopper explicitement le ranging
                i2c_bus_write_reg16addr(dev, REG_SYSTEM_MODE_START, 0x00);
                vTaskDelay(pdMS_TO_TICKS(10));
                
                // 2. Acquitter l'interruption
                i2c_bus_write_reg16addr(dev, REG_SYSTEM_INTERRUPT_CLEAR, 0x01);
                
                // 3. Relancer
                i2c_bus_write_reg16addr(dev, REG_SYSTEM_MODE_START, 0x40);
                ready_timeout_count = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t init_vl53l1x(void) {
    return xTaskCreate(vl53l1x_task, "vl53l1x_task", 3072, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

#else // !CONFIG_USE_VL53L1X

esp_err_t init_vl53l1x(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_VL53L1X
