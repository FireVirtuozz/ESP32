#ifndef PERIPHERALS_ADC_HELPER_H_
#define PERIPHERALS_ADC_HELPER_H_

#include <inttypes.h>
#include <esp_err.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_continuous.h"

// Generic building blocks for ADC-based sensors: single-shot polled reads
// (photoresistors, analog hall sensors, heartbeat sensors...) and
// continuous DMA-driven reads (multi-axis joysticks, anything needing two
// channels sampled together at a fixed rate).

/**
 * Opaque handle for a one-shot single-channel ADC read, with optional
 * calibration to convert raw counts to millivolts.
 */
typedef struct {
    adc_unit_t unit;
    adc_channel_t channel;
    adc_oneshot_unit_handle_t oneshot_handle;
    adc_cali_handle_t cali_handle; // NULL if calibration isn't supported/available
    bool cali_enabled;
} adc_oneshot_sensor_t;

/**
 * Configure a one-shot ADC channel.
 *
 * @param sensor  handle to initialize (unit/channel must already be set)
 * @param atten   attenuation (e.g. ADC_ATTEN_DB_12 for the full 0-3.3V range)
 */
esp_err_t adc_oneshot_sensor_init(adc_oneshot_sensor_t *sensor, adc_atten_t atten);

/**
 * Read a raw ADC value (0..4095 on a 12-bit ADC).
 */
esp_err_t adc_oneshot_sensor_read_raw(adc_oneshot_sensor_t *sensor, int *raw);

/**
 * Read a calibrated value in millivolts. Falls back to a rough estimate
 * from the raw value if calibration isn't available on this chip.
 */
esp_err_t adc_oneshot_sensor_read_mv(adc_oneshot_sensor_t *sensor, int *millivolts);

/**
 * Opaque handle for a continuous (DMA-driven) ADC read across up to two
 * channels sampled together (e.g. a joystick's X/Y axes).
 */
typedef struct {
    adc_continuous_handle_t handle;
    adc_channel_t channel_a;
    adc_channel_t channel_b;
    adc_unit_t unit;
} adc_continuous_dual_t;

/**
 * Configure continuous DMA-driven sampling across two channels on the same unit.
 *
 * @param sensor          handle to initialize (unit/channel_a/channel_b must already be set)
 * @param sample_freq_hz  total sampling frequency across both channels
 */
esp_err_t adc_continuous_dual_init(adc_continuous_dual_t *sensor, uint32_t sample_freq_hz);

/**
 * Read the latest available raw values for both channels.
 * Drains any buffered conversion frames and keeps only the most recent
 * sample of each channel.
 */
esp_err_t adc_continuous_dual_read(adc_continuous_dual_t *sensor, int *raw_a, int *raw_b);

#endif // PERIPHERALS_ADC_HELPER_H_
