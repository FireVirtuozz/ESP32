#ifndef KY039_H_
#define KY039_H_

#include <esp_err.h>

// KY-039: optical heartbeat (IR photoplethysmography) sensor, analog output.
// Sends the raw ADC reading as int32_t (no unit conversion), matching the
// original firmware exactly.
#define KY039_ADC_UNIT ADC_UNIT_1
#define KY039_ADC_CHANNEL ADC_CHANNEL_4 // GPIO32
#define KY039_PERIOD_MS 50

esp_err_t init_ky039(void);

#endif // KY039_H_
