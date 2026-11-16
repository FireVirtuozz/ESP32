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
#include "nvs_lib.h"

#include "VL53L1X_api.h"
#include "VL53L1X_calibration.h"
#include "vl53l1x_platform_glue.h"

static const char *TAG = "vl53l1x_sensor";

#if CONFIG_USE_VL53L1X

#define VL53L1X_DEV_ID                 0        // slot in the platform glue table
#define VL53L1X_EXPECTED_MODEL_ID      0xEACC
#define VL53L1X_BOOT_RETRIES           20
#define VL53L1X_BOOT_POLL_MS           10       // must be >= 1 tick (10ms @ default 100Hz tick rate)
#define VL53L1X_READY_TIMEOUT_CYCLES   100       // *10ms poll = 1s, same as before

// Distance mode: 1 = Short (matches your original short-mode config)
#define VL53L1X_DISTANCE_MODE_SHORT    1
#define VL53L1X_TIMING_BUDGET_MS       20
#define VL53L1X_INTER_MEASUREMENT_MS   25        // must stay >= timing budget
#define VL53L1X_ROI_WIDTH              8
#define VL53L1X_ROI_HEIGHT             8
#define VL53L1X_ROI_CENTER_SPAD        199

// NVS keys are capped at 15 chars by ESP-IDF, keep these short.
#define VL53L1X_NVS_KEY_OFFSET         "tof_offset"
#define VL53L1X_NVS_KEY_XTALK          "tof_xtalk"
// load_nvs_int() auto-creates a missing key at 0 and returns ESP_OK, so the
// error code alone can't tell "not calibrated yet" from "calibrated to 0".
// This flag is only ever set to 1 by run_calibration() on a successful save.
#define VL53L1X_NVS_KEY_CAL_OK          "tof_cal_ok"

static i2c_master_dev_handle_t dev;

static esp_err_t wait_for_boot(void) {
    for (int retry = 0; retry < VL53L1X_BOOT_RETRIES; retry++) {
        uint8_t boot_state = 0;
        VL53L1X_ERROR err = VL53L1X_BootState(VL53L1X_DEV_ID, &boot_state);
        log_msg(TAG, "boot poll %d: err=%d boot_state=0x%02X", retry, err, boot_state);
        // ST's own UM2510 reference loops `while (sensorState == 0)` - any
        // nonzero value means booted, not just 1 (see e.g. 0x03 in practice).
        if (err == VL53L1X_ERROR_NONE && boot_state != 0) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(VL53L1X_BOOT_POLL_MS));
    }
    return ESP_ERR_TIMEOUT;
}

// Reads offset/xtalk from NVS and applies them to the sensor. Call after
// SensorInit(), before StartRanging(). If nothing's been calibrated yet
// this just logs a warning and returns - ranging still works uncalibrated.
static esp_err_t apply_calibration(void) {
    // Check the explicit flag first - offset/xtalk being 0 doesn't tell us
    // whether that's a real calibration or just load_nvs_int's auto-init default.
    int calibrated = 0;
    load_nvs_int(VL53L1X_NVS_KEY_CAL_OK, &calibrated);
    if (calibrated != 1) {
        log_msg_lvl(ESP_LOG_WARN, TAG, "No stored calibration, running uncalibrated");
        return ESP_ERR_INVALID_RESPONSE;
    }

    // nvs_lib stores everything as i32; offset/xtalk both fit comfortably.
    int offset_val = 0;
    int xtalk_val = 0;
    esp_err_t err_offset = load_nvs_int(VL53L1X_NVS_KEY_OFFSET, &offset_val);
    esp_err_t err_xtalk = load_nvs_int(VL53L1X_NVS_KEY_XTALK, &xtalk_val);
    if (err_offset != ESP_OK || err_xtalk != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Calibration flag set but values unreadable, running uncalibrated");
        return ESP_FAIL;
    }

    VL53L1X_ERROR api_err = VL53L1X_ERROR_NONE;
    api_err |= VL53L1X_SetOffset(VL53L1X_DEV_ID, (int16_t)offset_val);
    api_err |= VL53L1X_SetXtalk(VL53L1X_DEV_ID, (uint16_t)xtalk_val);
    if (api_err != VL53L1X_ERROR_NONE) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed to apply calibration (err=%u)", api_err);
        return ESP_FAIL;
    }

    log_msg(TAG, "Applied calibration: offset=%d mm, xtalk=%d cps", offset_val, xtalk_val);
    return ESP_OK;
}

#if VL53L1X_CALIBRATE_ON_BOOT
// Bench-only: runs ST's offset then xtalk calibration (offset MUST run
// first, it zeroes the offset registers before measuring) and stores the
// result in NVS. Gated by VL53L1X_CALIBRATE_ON_BOOT in vl53l1x.h.
static esp_err_t run_calibration(void) {
    int16_t offset = 0;
    uint16_t xtalk = 0;

    log_msg(TAG, "Offset calibration at %u mm...", VL53L1X_CALIB_OFFSET_TARGET_MM);
    if (VL53L1X_CalibrateOffset(VL53L1X_DEV_ID, VL53L1X_CALIB_OFFSET_TARGET_MM, &offset) != VL53L1X_ERROR_NONE) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Offset calibration failed");
        return ESP_FAIL;
    }
    log_msg(TAG, "Offset found: %d mm", offset);

    log_msg(TAG, "Move the target to %u mm for xtalk calibration...", VL53L1X_CALIB_XTALK_TARGET_MM);
    for (int s = VL53L1X_CALIB_MOVE_DELAY_S; s > 0; s--) {
        log_msg(TAG, "Xtalk calibration starts in %d s...", s);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    log_msg(TAG, "Xtalk calibration at %u mm...", VL53L1X_CALIB_XTALK_TARGET_MM);
    if (VL53L1X_CalibrateXtalk(VL53L1X_DEV_ID, VL53L1X_CALIB_XTALK_TARGET_MM, &xtalk) != VL53L1X_ERROR_NONE) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Xtalk calibration failed");
        return ESP_FAIL;
    }
    log_msg(TAG, "Xtalk found: %u cps", xtalk);

    esp_err_t err_offset = save_nvs_int(VL53L1X_NVS_KEY_OFFSET, (int)offset);
    esp_err_t err_xtalk = save_nvs_int(VL53L1X_NVS_KEY_XTALK, (int)xtalk);
    if (err_offset != ESP_OK || err_xtalk != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed to store calibration (offset=%s, xtalk=%s)",
            esp_err_to_name(err_offset), esp_err_to_name(err_xtalk));
        return ESP_FAIL;
    }
    esp_err_t err_flag = save_nvs_int(VL53L1X_NVS_KEY_CAL_OK, 1);
    if (err_flag != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Values stored but failed to set calibrated flag: %s",
            esp_err_to_name(err_flag));
        return err_flag;
    }

    log_msg(TAG, "Calibration stored — set VL53L1X_CALIBRATE_ON_BOOT back to 0 and reflash");
    return ESP_OK;
}
#endif // VL53L1X_CALIBRATE_ON_BOOT

//VL53L1X_SetInterruptPolarity -- modify if using interruptions
//VL53L1X_GetSignalPerSpad -- already in result
//VL53L1X_GetAmbientPerSpad
//VL53L1X_GetSignalRate
//VL53L1X_GetSpadNb -- already in result
//VL53L1X_GetAmbientRate -- already in result
//VL53L1X_GetRangeStatus -- already in result
//VL53L1X_SetDistanceThreshold -- window useful for interruptions
//VL53L1X_SetSignalThreshold -- threshold of signal sensor considers not valid result
//VL53L1X_SetSigmaThreshold -- threshold of noise sensor considers not valid result
//VL53L1X_StartTemperatureUpdate

static char* get_str_window_mode(uint16_t window_mode) {
    //(0=below; 1=above; 2=out; 3=in)
    switch (window_mode)
    {
    case 0: return "below";
    case 1: return "above";
    case 2: return "out";
    case 3: return "in";
    default: return "unknown mode";
    }
}

#define VL53L1X_RESULT_SIZE (4 * sizeof(uint16_t) + sizeof(uint8_t))

static void serialize_vl53l1x_result(uint8_t *buf, VL53L1X_Result_t *vl53l1x) {
    memcpy(&buf[0], &vl53l1x->Distance, 2);
    memcpy(&buf[2], &vl53l1x->Ambient, 2);
    memcpy(&buf[4], &vl53l1x->SigPerSPAD, 2);
    memcpy(&buf[6], &vl53l1x->NumSPADs, 2);
    buf[8] = vl53l1x->Status;
}

static void vl53l1x_task(void *params) {
    (void)params;

    // XSHUT sequence unchanged (DS12385 3.6, Option 1)
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
    vl53l1x_platform_bind(VL53L1X_DEV_ID, dev);

    if (wait_for_boot() != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Firmware boot timeout");
        vTaskDelete(NULL);
        return;
    }

    uint16_t model_id = 0;
    if (VL53L1X_GetSensorId(VL53L1X_DEV_ID, &model_id) != VL53L1X_ERROR_NONE
        || model_id != VL53L1X_EXPECTED_MODEL_ID) {
        log_msg_lvl(ESP_LOG_ERROR, TAG,
            "Unexpected model ID 0x%04X (expected 0x%04X), check wiring/XSHUT",
            model_id, VL53L1X_EXPECTED_MODEL_ID);
        vTaskDelete(NULL);
        return;
    }

    // VL53L1X_SensorInit() loads ST's 91-byte default config table, fires one
    // ranging cycle to confirm the sensor actually answers, clears the
    // interrupt and stops ranging again.
    if (VL53L1X_SensorInit(VL53L1X_DEV_ID) != VL53L1X_ERROR_NONE) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "SensorInit failed");
        vTaskDelete(NULL);
        return;
    }

#if VL53L1X_CALIBRATE_ON_BOOT
    if (run_calibration() != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Calibration failed");
        vTaskDelete(NULL);
        return;
    }
#endif
    apply_calibration(); // no-op (logs a warning) if nothing stored yet

    VL53L1X_ERROR cfg_err = VL53L1X_ERROR_NONE;
    cfg_err |= VL53L1X_SetDistanceMode(VL53L1X_DEV_ID, VL53L1X_DISTANCE_MODE_SHORT);
    cfg_err |= VL53L1X_SetROI(VL53L1X_DEV_ID, VL53L1X_ROI_WIDTH, VL53L1X_ROI_HEIGHT);
    cfg_err |= VL53L1X_SetROICenter(VL53L1X_DEV_ID, VL53L1X_ROI_CENTER_SPAD);
    // SetTimingBudget before InterMeasurement: the ULD API encodes the
    // inter-measurement period from the currently configured timing budget
    // (see VL53L1X_SetInterMeasurementInMs in VL53L1X_api.c) — order matters.
    cfg_err |= VL53L1X_SetTimingBudgetInMs(VL53L1X_DEV_ID, VL53L1X_TIMING_BUDGET_MS);
    cfg_err |= VL53L1X_SetInterMeasurementInMs(VL53L1X_DEV_ID, VL53L1X_INTER_MEASUREMENT_MS);
    if (cfg_err != VL53L1X_ERROR_NONE) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Sensor configuration failed (err=%u)", cfg_err);
        vTaskDelete(NULL);
        return;
    }

    VL53L1X_Version_t sw_version;
    if (VL53L1X_GetSWVersion(&sw_version) != VL53L1X_ERROR_NONE) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed to get SW version");
        vTaskDelete(NULL);
        return;
    } else {
        log_msg(TAG, "VL53L1X SW version (M:%u, m:%u, b:%u, r:%u)",
            sw_version.major, sw_version.minor, sw_version.build, sw_version.revision);
    }

    uint16_t tb; //distance for ranging
    if (VL53L1X_GetTimingBudgetInMs(VL53L1X_DEV_ID, &tb) == VL53L1X_ERROR_NONE) {
        log_msg(TAG, "VL53L1X Timing budget applied: %u ms", tb);
    }

    uint16_t im; //distance between two ranging
    if (VL53L1X_GetInterMeasurementInMs(VL53L1X_DEV_ID, &im) == VL53L1X_ERROR_NONE) {
        log_msg(TAG, "VL53L1X Intermeasurement mode applied: %u ms", im);
    }

    uint16_t dm;
    if (VL53L1X_GetDistanceMode(VL53L1X_DEV_ID, &dm) == VL53L1X_ERROR_NONE) {
        log_msg(TAG, "VL53L1X Distance mode applied: %s", dm == 2 ? "long" : "short");
    }

    int16_t of;
    if (VL53L1X_GetOffset(VL53L1X_DEV_ID, &of) == VL53L1X_ERROR_NONE) {
        log_msg(TAG, "VL53L1X offset calibration applied: %d mm", of);
    }

    uint16_t xt;
    if (VL53L1X_GetXtalk(VL53L1X_DEV_ID, &xt) == VL53L1X_ERROR_NONE) {
        log_msg(TAG, "VL53L1X xtalk calibration applied: %u", xt);
    }

    uint16_t win_mode;
    if (VL53L1X_GetDistanceThresholdWindow(VL53L1X_DEV_ID, &win_mode) == VL53L1X_ERROR_NONE) {
        log_msg(TAG, "VL53L1X window detection mode: %s", get_str_window_mode(win_mode));
    }

    uint16_t low;
    if (VL53L1X_GetDistanceThresholdLow(VL53L1X_DEV_ID, &low) == VL53L1X_ERROR_NONE) {
        log_msg(TAG, "VL53L1X window low: %u", low);
    }

    uint16_t high;
    if (VL53L1X_GetDistanceThresholdHigh(VL53L1X_DEV_ID, &high) == VL53L1X_ERROR_NONE) {
        log_msg(TAG, "VL53L1X window high: %u", high);
    }

    uint16_t x, y;
    if (VL53L1X_GetROI_XY(VL53L1X_DEV_ID, &x, &y) == VL53L1X_ERROR_NONE) {
        log_msg(TAG, "VL53L1X ROI, x: %u; y: %u", x, y);
    }

    uint8_t roi_center;
    if (VL53L1X_GetROICenter(VL53L1X_DEV_ID, &roi_center) == VL53L1X_ERROR_NONE) {
        log_msg(TAG, "VL53L1X ROI center: %u", roi_center);
    }

    uint16_t st;
    if (VL53L1X_GetSignalThreshold(VL53L1X_DEV_ID, &st) == VL53L1X_ERROR_NONE) {
        log_msg(TAG, "VL53L1X signal threshold: %u", st);
    }
    
    uint16_t sig;
    if (VL53L1X_GetSigmaThreshold(VL53L1X_DEV_ID, &sig) == VL53L1X_ERROR_NONE) {
        log_msg(TAG, "VL53L1X sigma threshold: %u", sig);
    }

    uint8_t inte;
    if (VL53L1X_GetInterruptPolarity(VL53L1X_DEV_ID, &inte) == VL53L1X_ERROR_NONE) {
        log_msg(TAG, "VL53L1X interruption GPIO1: %s", inte ? "active" : "not active");
    }

    if (VL53L1X_StartRanging(VL53L1X_DEV_ID) != VL53L1X_ERROR_NONE) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "StartRanging failed");
        vTaskDelete(NULL);
        return;
    }

    log_msg(TAG, "VL53L1X initialized at address 0x%02X (XSHUT: GPIO%d)",
        VL53L1X_I2C_ADDR, VL53L1X_XSHUT_GPIO);

    uint8_t ready_timeout_count = 0;

    while (true) {
        uint8_t ready = 0;
        if (VL53L1X_CheckForDataReady(VL53L1X_DEV_ID, &ready) == VL53L1X_ERROR_NONE && ready) {
            ready_timeout_count = 0;

            // GetResult() pulls status + distance + ambient + signal + SPAD
            // count in one 17-byte multi-read, instead of two separate
            // transactions. Also gives you ambient/signal for free next time
            // you need to debug a HARDWARE_FAIL / low-signal case.
            VL53L1X_Result_t result;
            if (VL53L1X_GetResult(VL53L1X_DEV_ID, &result) != VL53L1X_ERROR_NONE) {
                log_msg_lvl(ESP_LOG_ERROR, TAG, "GetResult failed");
                VL53L1X_ClearInterrupt(VL53L1X_DEV_ID);
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            VL53L1X_ClearInterrupt(VL53L1X_DEV_ID); // always clear, same as before

            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_VL53L1X;
            uint8_t buf[HEADER_SENSOR_SIZE + VL53L1X_RESULT_SIZE];
            serialize_header(&header, buf);
            serialize_vl53l1x_result(&buf[HEADER_SENSOR_SIZE], &result);

#if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
#endif

            switch (result.Status) { // 0 = valid measurement (ST ULD status codes)
                case 0: {
/*
                    log_msg(TAG, "Distance: %u mm (ambient %u, sigPerSpad %u, spads %u)",
                        result.Distance, result.Ambient, result.SigPerSPAD, result.NumSPADs);
*/
                    break;
                }
                case 2:
                    break;
                default:
                    log_msg(TAG, "Invalid range, raw distance %u mm, status %u",
                        result.Distance, result.Status);
                    break;
            }
        } else {
            ready_timeout_count++;
            if (ready_timeout_count >= VL53L1X_READY_TIMEOUT_CYCLES) {
                log_msg_lvl(ESP_LOG_WARN, TAG, "Sensor hung, resetting ranging sequence...");
                VL53L1X_StopRanging(VL53L1X_DEV_ID);
                vTaskDelay(pdMS_TO_TICKS(10));
                VL53L1X_ClearInterrupt(VL53L1X_DEV_ID);
                VL53L1X_StartRanging(VL53L1X_DEV_ID);
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