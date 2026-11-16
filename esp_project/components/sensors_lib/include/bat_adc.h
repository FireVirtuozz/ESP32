#ifndef BAT_ADC_H
#define BAT_ADC_H

#include "peripherals/adc_helper.h"
#include "esp_err.h"

/**
 * @brief Init ADC Bat tasks
 */
esp_err_t init_bat_adc();

#endif // BAT_ADC_H