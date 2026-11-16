#include "peripherals/adc_helper.h"
#include "log_lib.h"
#include <string.h>
#include <esp_adc/adc_cali_scheme.h>

static const char *TAG = "adc_helper_peripheral";

// --- One-shot ---

esp_err_t adc_oneshot_sensor_init(adc_oneshot_sensor_t *sensor, adc_atten_t atten) {
    if (sensor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = sensor->unit,
    };
    esp_err_t err = adc_oneshot_new_unit(&init_cfg, &sensor->oneshot_handle);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) creating ADC oneshot unit", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(sensor->oneshot_handle, sensor->channel, &chan_cfg);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) configuring ADC channel %d", esp_err_to_name(err), sensor->channel);
        return err;
    }
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = sensor->unit,
        .chan = sensor->channel,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_config, &sensor->cali_handle);
#else
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_0,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_line_fitting(&cali_config, &sensor->cali_handle);
#endif
    sensor->cali_enabled = (err == ESP_OK);
    if (!sensor->cali_enabled) {
        log_msg(TAG, "ADC calibration unavailable on this chip/atten (%s), raw-to-mV will be an estimate",
            esp_err_to_name(err));
    }

    log_msg(TAG, "ADC oneshot channel %d initialized (unit %d)", sensor->channel, sensor->unit);
    return ESP_OK;
}

esp_err_t adc_oneshot_sensor_read_raw(adc_oneshot_sensor_t *sensor, int *raw) {
    if (sensor == NULL || raw == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return adc_oneshot_read(sensor->oneshot_handle, sensor->channel, raw);
}

esp_err_t adc_oneshot_sensor_read_mv(adc_oneshot_sensor_t *sensor, int *millivolts) {
    if (sensor == NULL || millivolts == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int raw;
    esp_err_t err = adc_oneshot_sensor_read_raw(sensor, &raw);
    if (err != ESP_OK) {
        return err;
    }

    if (sensor->cali_enabled) {
        return adc_cali_raw_to_voltage(sensor->cali_handle, raw, millivolts);
    }

    // Rough fallback estimate assuming a 0-3300mV / 12-bit range.
    *millivolts = (raw * 3300) / 4095;
    return ESP_OK;
}

// --- Continuous dual-channel ---

esp_err_t adc_continuous_dual_init(adc_continuous_dual_t *sensor, uint32_t sample_freq_hz) {
    if (sensor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    adc_continuous_handle_cfg_t handle_cfg = {
        .max_store_buf_size = 1024,
        .conv_frame_size = 256,
    };
    esp_err_t err = adc_continuous_new_handle(&handle_cfg, &sensor->handle);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) creating continuous ADC handle", esp_err_to_name(err));
        return err;
    }

    adc_digi_pattern_config_t patterns[2] = {
        {
            .atten = ADC_ATTEN_DB_12,
            .channel = sensor->channel_a,
            .unit = sensor->unit,
            .bit_width = ADC_BITWIDTH_DEFAULT,
        },
        {
            .atten = ADC_ATTEN_DB_12,
            .channel = sensor->channel_b,
            .unit = sensor->unit,
            .bit_width = ADC_BITWIDTH_DEFAULT,
        },
    };

    adc_continuous_config_t continuous_config = {
        .sample_freq_hz = sample_freq_hz,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
        .pattern_num = 2,
        .adc_pattern = patterns,
    };

    err = adc_continuous_config(sensor->handle, &continuous_config);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) configuring continuous ADC", esp_err_to_name(err));
        return err;
    }

    err = adc_continuous_start(sensor->handle);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) starting continuous ADC", esp_err_to_name(err));
        return err;
    }

    log_msg(TAG, "Continuous ADC initialized (channels %d/%d, unit %d)",
        sensor->channel_a, sensor->channel_b, sensor->unit);
    return ESP_OK;
}

esp_err_t adc_continuous_dual_read(adc_continuous_dual_t *sensor, int *raw_a, int *raw_b) {
    if (sensor == NULL || raw_a == NULL || raw_b == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t result[256] = {0};
    uint32_t bytes_read = 0;
    bool got_a = false, got_b = false;

    esp_err_t err = adc_continuous_read(sensor->handle, result, sizeof(result), &bytes_read, 0);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
        log_msg(TAG, "Error (%s) reading continuous ADC", esp_err_to_name(err));
        return err;
    }

    for (uint32_t i = 0; i < bytes_read; i += SOC_ADC_DIGI_RESULT_BYTES) {
        adc_digi_output_data_t *p = (adc_digi_output_data_t *)&result[i];
        if (p->type1.channel == sensor->channel_a) {
            *raw_a = p->type1.data;
            got_a = true;
        } else if (p->type1.channel == sensor->channel_b) {
            *raw_b = p->type1.data;
            got_b = true;
        }
    }

    if (!got_a || !got_b) {
        return ESP_ERR_NOT_FOUND; // no fresh sample for at least one channel this call
    }
    return ESP_OK;
}
