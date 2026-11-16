#include "peripherals/adc_helper.h"
#include "log_lib.h"
#include <string.h>
#include <esp_adc/adc_cali_scheme.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "adc_helper_peripheral";

// --- One-shot ---
esp_err_t adc_oneshot_sensor_init(adc_oneshot_sensor_t *sensor, adc_atten_t atten) {
    if (sensor == NULL) return ESP_ERR_INVALID_ARG;

    adc_oneshot_unit_init_cfg_t init_cfg = { .unit_id = sensor->unit };
    esp_err_t err = adc_oneshot_new_unit(&init_cfg, &sensor->oneshot_handle);
    if (err != ESP_OK) return err;

    adc_oneshot_chan_cfg_t chan_cfg = { .atten = atten, .bitwidth = ADC_BITWIDTH_DEFAULT };
    err = adc_oneshot_config_channel(sensor->oneshot_handle, sensor->channel, &chan_cfg);
    if (err != ESP_OK) return err;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = sensor->unit, .chan = sensor->channel, .atten = atten, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_config, &sensor->cali_handle);
#else
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1, .atten = ADC_ATTEN_DB_0, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_line_fitting(&cali_config, &sensor->cali_handle);
#endif
    sensor->cali_enabled = (err == ESP_OK);
    return ESP_OK;
}

esp_err_t adc_oneshot_sensor_read_raw(adc_oneshot_sensor_t *sensor, int *raw) {
    if (sensor == NULL || raw == NULL) return ESP_ERR_INVALID_ARG;
    return adc_oneshot_read(sensor->oneshot_handle, sensor->channel, raw);
}

esp_err_t adc_oneshot_sensor_read_mv(adc_oneshot_sensor_t *sensor, int *millivolts) {
    if (sensor == NULL || millivolts == NULL) return ESP_ERR_INVALID_ARG;
    int raw;
    if (adc_oneshot_sensor_read_raw(sensor, &raw) != ESP_OK) return ESP_FAIL;
    if (sensor->cali_enabled) {
        return adc_cali_raw_to_voltage(sensor->cali_handle, raw, millivolts);
    }
    *millivolts = (raw * 3300) / 4095;
    return ESP_OK;
}

// ------ Continuous (DMA) ------

#define SAMPLE_FREQUENCY 20000

struct adc_channel_handle_st {
    adc_channel_t channel;
    volatile uint32_t last_raw;
    bool active;
};

// Tableau indexé par le numéro de canal physique (ex: 0 à 7 sur ADC1)
static struct adc_channel_handle_st s_channels[8] = {0};
static adc_continuous_handle_t s_dma_handle = NULL;
static TaskHandle_t s_task_handle = NULL;
static bool s_dma_running = false;
static SemaphoreHandle_t s_adc_mutex = NULL;

static esp_err_t reconfigure_and_start_dma(void) {
    esp_err_t err;
    if (s_adc_mutex == NULL) {
        s_adc_mutex = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(s_adc_mutex, portMAX_DELAY);

    // 1. Destruction complète du handle existant
    if (s_dma_handle != NULL) {
        err = adc_continuous_stop(s_dma_handle);
        if (err != ESP_OK) {
            xSemaphoreGive(s_adc_mutex);
            return err;
        }
        err = adc_continuous_deinit(s_dma_handle);
        if (err != ESP_OK) {
            xSemaphoreGive(s_adc_mutex);
            return err;
        }
        s_dma_handle = NULL;
    }
    s_dma_running = false;

    // 2. Recréation propre unique
    adc_continuous_handle_cfg_t handle_cfg = {
        .max_store_buf_size = 4096,
        .conv_frame_size = 512,
    };
    err = adc_continuous_new_handle(&handle_cfg, &s_dma_handle);
    if (err != ESP_OK) {
        xSemaphoreGive(s_adc_mutex);
        return err;
    }

    // 3. Configuration des patterns
    adc_digi_pattern_config_t patterns[8] = {0};
    uint8_t active_count = 0;

    for (int i = 0; i < 8; i++) {
        if (s_channels[i].active) {
            patterns[active_count].atten = ADC_ATTEN_DB_12;
            patterns[active_count].channel = s_channels[i].channel;
            patterns[active_count].unit = ADC_UNIT_1;
            patterns[active_count].bit_width = ADC_BITWIDTH_12;
            active_count++;
        }
    }

    if (active_count == 0) {
        xSemaphoreGive(s_adc_mutex);
        return ESP_OK;
    }

    adc_continuous_config_t continuous_config = {
        .sample_freq_hz = SAMPLE_FREQUENCY,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
#if CONFIG_IDF_TARGET_ESP32
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
#else
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
#endif
        .pattern_num = active_count,
        .adc_pattern = patterns,
    };

    err = adc_continuous_config(s_dma_handle, &continuous_config);
    if (err != ESP_OK) {
        xSemaphoreGive(s_adc_mutex);
        return err;
    }

    err = adc_continuous_start(s_dma_handle);
    if (err != ESP_OK) {
        xSemaphoreGive(s_adc_mutex);
        return err;
    }

    s_dma_running = true;
    xSemaphoreGive(s_adc_mutex);
    return ESP_OK;
}

static void adc_manager_task(void *params) {
    uint8_t result[256] = {0};
    uint32_t bytes_read = 0;

    while (true) {
        if (!s_dma_running) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        esp_err_t err = adc_continuous_read(s_dma_handle, result, sizeof(result), &bytes_read, pdMS_TO_TICKS(10));

        if (err != ESP_OK || bytes_read == 0) {
            // Si timeout ou erreur, on laisse respirer le CPU (évite le 100% en boucle)
            //log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed reading continuous: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        uint32_t sum_raw[8] = {0};
        uint32_t count_raw[8] = {0};

        for (uint32_t i = 0; i < bytes_read; i += SOC_ADC_DIGI_RESULT_BYTES) {
            adc_digi_output_data_t *p = (adc_digi_output_data_t *)&result[i];
#if CONFIG_IDF_TARGET_ESP32
            uint8_t chan = p->type1.channel;
            uint32_t val = p->type1.data;
#else
                uint8_t chan = p->type2.channel;
                uint32_t val = p->type2.data;
#endif

            if (chan < 8 && s_channels[chan].active) {
                sum_raw[chan] += val;
                count_raw[chan]++;
            }
        }

        // Calcul de la moyenne sur le buffer DMA pour lisser le bruit
        for (int i = 0; i < 8; i++) {
            if (s_channels[i].active && count_raw[i] > 0) {
                s_channels[i].last_raw = sum_raw[i] / count_raw[i];
            }
        }
    }
}

adc_channel_handle_t adc_manager_register_channel(adc_channel_t channel) {
    if (channel >= 8) return NULL;

    s_channels[channel].channel = channel;
    s_channels[channel].last_raw = 0;
    s_channels[channel].active = true;

    if (reconfigure_and_start_dma() != ESP_OK) {
        s_channels[channel].active = false;
        return NULL;
    }

    if (s_task_handle == NULL) {
        xTaskCreate(adc_manager_task, "adc_mgr_task", 2560, NULL, 5, &s_task_handle);
    }

    log_msg(TAG, "Canal ADC %d enregistré et DMA mis à jour", channel);
    return &s_channels[channel];
}

uint32_t adc_manager_get_raw(adc_channel_handle_t handle) {
    return handle ? handle->last_raw : 0;
}