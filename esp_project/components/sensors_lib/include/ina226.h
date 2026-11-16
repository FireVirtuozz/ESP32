#ifndef INA226_H_
#define INA226_H_

#include <esp_err.h>

// INA226: current/voltage/power monitor over I2C.
// Wire format sends RAW register values (int16/uint16), matching the
// original firmware exactly — conversion to physical units (mV, mA, mW)
// happens client-side, not on the ESP.
#define INA226_I2C_ADDR 0x40

esp_err_t init_ina226(void);

#endif // INA226_H_
