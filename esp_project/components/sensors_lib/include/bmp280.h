#ifndef BMP280_H_
#define BMP280_H_

#include <esp_err.h>

// BMP280: barometric pressure + temperature sensor over I2C.
// Wire format sends Bosch's raw fixed-point compensated int32_t values
// (matching the original firmware exactly), NOT floats: temperature in
// hundredths of a degree C, pressure in Bosch's Q24.8-style raw units
// (value * 1e-5 gives bar client-side).
#define BMP280_I2C_ADDR 0x76

esp_err_t init_bmp280(void);

#endif // BMP280_H_
