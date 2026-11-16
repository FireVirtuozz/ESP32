#ifndef AS5600_H_
#define AS5600_H_

#include <esp_err.h>

// AS5600: 12-bit magnetic rotary position sensor over I2C.
// Reads the ANGLE register (0x0E, post-filter output) — NOT RAW_ANGLE
// (0x0C) — and sends the 2 raw register bytes over the wire as-is,
// matching the original firmware exactly (no masking/repacking on-device).
#define AS5600_I2C_ADDR 0x36
#define AS5600_REG_ANGLE 0x0E
#define AS5600_PERIOD_MS 100

esp_err_t init_as5600(void);

#endif // AS5600_H_
