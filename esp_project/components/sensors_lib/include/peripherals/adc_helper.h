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

#define ADC_MGR_MAX_CHANNELS 6

// Déclaration du type opaque pour les drivers
typedef struct adc_channel_handle_st* adc_channel_handle_t;

/**
 * @brief Enregistre un canal et reconfigure le DMA automatiquement
 */
adc_channel_handle_t adc_manager_register_channel(adc_channel_t channel);

/**
 * @brief Lit la dernière valeur brute (raw) lissée pour un canal
 */
uint32_t adc_manager_get_raw(adc_channel_handle_t handle);

#endif // PERIPHERALS_ADC_HELPER_H_
