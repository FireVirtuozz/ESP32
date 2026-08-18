#ifndef KY023_H_
#define KY023_H_

#include <esp_err.h>

// KY-023: dual-axis analog joystick with push button.
#define KY023_ADC_UNIT ADC_UNIT_1
#define KY023_X_CHANNEL ADC_CHANNEL_0
#define KY023_Y_CHANNEL ADC_CHANNEL_3
#define KY023_SW_GPIO 22

esp_err_t init_ky023(void);

#endif // KY023_H_
