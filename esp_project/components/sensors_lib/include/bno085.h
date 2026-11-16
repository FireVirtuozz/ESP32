#ifndef BNO085_H_
#define BNO085_H_

#include <esp_err.h>

// BNO085: 9-axis IMU with on-chip sensor fusion (SH2/SHTP protocol over I2C).
// Source: CEVA sh2 driver (https://github.com/ceva-dsp/sh2) + SH-2 Reference
// Manual. Runs on the project's shared I2C bus (see i2c_helper.c: SCL=25,
// SDA=26) - no separate SDA/SCL pins needed here.
#define BNO085_I2C_ADDR 0x4A   // default 7-bit address (0x4B if ADR pin is pulled)

// RESET must be driven by a free GPIO - the sh2 HAL's open() needs it to put
// the sensor hub in a known state before talking to it.
#define BNO085_RESET_GPIO 6

// H_INTN (active low): the BNO085 only has data ready for a fixed window
// after asserting this pin. Reading without checking it first risks a NACK
// or an I2C timeout from the sensor's clock stretching - see
// https://github.com/tstellanova/bno080/issues/5. Not optional in practice.
#define BNO085_INT_GPIO   7

// Rotation vector (quaternion) report interval, in microseconds.
// 20000us = 50Hz, plenty for a PC-side model; sh2 caps effective rate at
// whatever the chip's internal fusion loop supports regardless.
#define BNO085_REPORT_INTERVAL_US 20000

esp_err_t init_bno085(void);

// Feed a signed wheel speed sample (single FC33 encoder, signed using the
// BTS7960 motor command direction: forward=+, reverse=-) into the dead
// reckoning fusion. Thread-safe: call from any task, the value is only
// actually pushed to the sensor from bno085_task's own thread on its next
// service loop iteration. Units are whatever raw counts/sec your pcnt
// encoder produces - the SH-2 wheelData scale isn't documented publicly,
// treat this as unverified until validated empirically.
void bno085_set_wheel_speed(int16_t signed_speed);

#endif // BNO085_H_