
#include "bat_adc.h"
#include "log_lib.h"
#include "sensors_lib.h"

#if CONFIG_USE_UDPLIB
#include "udp_lib.h"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#if CONFIG_USE_BAT_ADC

#define R1 10.0f
#define R2 5.1f
#define DIVIDER_RATIO ((R1 + R2) / R2)
#define ADC_UNIT ADC_UNIT_1
#define ADC_CHANNEL ADC_CHANNEL_7

#define BAT_ADC_PACKET_SIZE (sizeof(float))

static adc_cali_handle_t bat_cali_handle = NULL;
static bool bat_cali_enabled = false;

static const char *TAG = "bat_driver";
static adc_channel_handle_t battery_channel_handle = NULL;

static void bat_adc_task(void *params) {
    (void)params;

    float filtered_voltage = 7.3f;
    const float alpha = 0.1f;

    log_msg(TAG, "Battery ADC init OK (Ratio: %.3f)", DIVIDER_RATIO);

    while (true) {

        uint32_t raw = adc_manager_get_raw(battery_channel_handle);

        int v_adc_mv = 0;
        if (bat_cali_enabled) {
            adc_cali_raw_to_voltage(bat_cali_handle, raw, &v_adc_mv);
        } else {
            v_adc_mv = (raw * 3300) / 4095; // Fallback
        }
        float voltage_volts = (v_adc_mv * DIVIDER_RATIO) / 1000.0f;
        filtered_voltage = (alpha * voltage_volts) + ((1.0f - alpha) * filtered_voltage);
        //log_msg(TAG, "Battery voltage: %.2f V (raw: %lu, v_adc_mv: %d, volt: %.2f)", 
        //    filtered_voltage, raw, v_adc_mv, voltage_volts);

        header_sensor_t header = {0};
        header.esp_id = (uint8_t)CONFIG_ESP_ID;
        header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
        header.type = SENSOR_TYPE_BAT_ADC;
        uint8_t buf[HEADER_SENSOR_SIZE + BAT_ADC_PACKET_SIZE];
        serialize_header(&header, buf);
        memcpy(&buf[HEADER_SENSOR_SIZE], &filtered_voltage, sizeof(float));

#if CONFIG_USE_UDPLIB
        send_udp_sensor(buf, sizeof(buf));
#endif
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t init_bat_adc(void) {
    battery_channel_handle = adc_manager_register_channel(ADC_CHANNEL);
    if (battery_channel_handle == NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed to register Battery ADC channel");
        return ESP_FAIL;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
    #if CONFIG_IDF_TARGET_ESP32
        .bitwidth = ADC_BITWIDTH_12,
    #else
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    #endif
    };
    esp_err_t err = adc_cali_create_scheme_curve_fitting(&cali_config, &bat_cali_handle);
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
    esp_err_t err = adc_cali_create_scheme_line_fitting(&cali_config, &bat_cali_handle);
#endif
    bat_cali_enabled = (err == ESP_OK);

    return xTaskCreate(bat_adc_task, "bat_adc_task", 2560, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

#else // !CONFIG_USE_BAT_ADC

esp_err_t init_bat_adc(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_BAT_ADC