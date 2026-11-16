#ifndef ACS758_H
#define ACS758_H

#include "peripherals/adc_helper.h"
#include "esp_err.h"


/**
 * @brief Initialize ACS758 in ADC continuous mode
 */
esp_err_t init_acs758();

#endif // ACS758_H