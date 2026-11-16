#include "acs758.h"
#include "log_lib.h"
#include "sensors_lib.h"

#if CONFIG_USE_ACS758

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#if CONFIG_USE_UDPLIB
#include "udp_lib.h"
#endif

static const char *TAG = "acs758_driver";

#define VCC_MV 3300.0f //mV
//#define ZERO_OFFSET (VCC_MV / 2.0f)
#define ZERO_OFFSET (1662.0f) //on datasheet (vcc/2), measured with multimeter
#define SENSITIVITY_NOMINAL 40.0f //for 50B, mV/A (see datasheet, for 5V)
#define SENSITIVITY (SENSITIVITY_NOMINAL * VCC_MV / 5000.0f)
//#define SENSITIVITY 34.6f

#define ACS_758_PACKET_SIZE (sizeof(float))

static adc_cali_handle_t acs_cali_handle = NULL;
static bool acs_cali_enabled = false;

static adc_channel_handle_t s_acs_handle = NULL;

static void acs758_task(void *params) {
    (void)params;

    log_msg(TAG, "ACS758 init OK (Sensitivity: %.2f mV/A, Zero: %.1f mV)", 
        SENSITIVITY, ZERO_OFFSET);

    float filtered_current = 0.0f;
    const float alpha = 0.05f;

    while (true) {
            uint32_t raw = adc_manager_get_raw(s_acs_handle);

            int v_adc_mv = 0;
            if (acs_cali_enabled) {
                adc_cali_raw_to_voltage(acs_cali_handle, raw, &v_adc_mv);
            } else {
                v_adc_mv = (raw * 3300) / 4095; // Fallback
            }
            float current_amps = ((float)v_adc_mv - ZERO_OFFSET) / SENSITIVITY;
            filtered_current = (alpha * current_amps) + ((1.0f - alpha) * filtered_current);

            //log_msg(TAG, "Battery current: %.2f A (raw: %u, v_adc_mv: %d, curr: %.2f)",
            //    filtered_current, raw, v_adc_mv, current_amps);

#if CONFIG_USE_UDPLIB
            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_ACS758;
            uint8_t buf[HEADER_SENSOR_SIZE + ACS_758_PACKET_SIZE];
            serialize_header(&header, buf);
            memcpy(&buf[HEADER_SENSOR_SIZE], &filtered_current, sizeof(float));

            send_udp_sensor(buf, sizeof(buf));
#endif
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t init_acs758(void) {
    //register before laucnhing task, ensuring it is well init before getting access to buffer
    s_acs_handle = adc_manager_register_channel(ADC_CHANNEL_4);
    if (s_acs_handle == NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed to register ADC channel");
        return ESP_ERR_NO_MEM;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CHANNEL_4,
        .atten = ADC_ATTEN_DB_12,
    #if CONFIG_IDF_TARGET_ESP32
        .bitwidth = ADC_BITWIDTH_12,
    #else
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    #endif
    };
    esp_err_t err = adc_cali_create_scheme_curve_fitting(&cali_config, &acs_cali_handle);
#else
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
    #if CONFIG_IDF_TARGET_ESP32
        .bitwidth = ADC_BITWIDTH_12,
    #else
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    #endif
    };
    esp_err_t err = adc_cali_create_scheme_line_fitting(&cali_config, &acs_cali_handle);
#endif
    acs_cali_enabled = (err == ESP_OK);

    return xTaskCreate(acs758_task, "acs758_task", 2048, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

#else // !CONFIG_USE_ACS758

esp_err_t init_acs758(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_ACS758