#ifndef KY018_H_
#define KY018_H_

#include <esp_err.h>

// KY-018: photoresistor (LDR) module, analog output.
// Sends the raw ADC reading as int32_t (no unit conversion), matching the
// original firmware exactly. Attenuation DB_0 (~0-1.1V range) — NOT DB_12 —
// also matches the original.
#define KY018_ADC_UNIT ADC_UNIT_1
#define KY018_ADC_CHANNEL ADC_CHANNEL_7
#define KY018_PERIOD_MS 1000

esp_err_t init_ky018(void);

#endif // KY018_H_
