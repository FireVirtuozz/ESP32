#ifndef VL53L1X_H_
#define VL53L1X_H_

#include <esp_err.h>

// VL53L1X: time-of-flight laser distance sensor over I2C.
// Sources: ST datasheet DS12385, ST API user manual UM2356, and — where
// those two don't go down to register level — well-established community
// reference ports (Pololu's VL53L1X driver and derivatives). See comments
// in vl53l1x.c for exactly which parts come from which source.
#define VL53L1X_I2C_ADDR 0x29

// XSHUT must be driven by a free GPIO (DS12385 section 3.6, "Option 1").
// Pick one that isn't shared with another sensor in this project - GPIO23
// is used by several other test sensors (DHT11, KY005, FC33, KY002...)
// and WILL conflict if you enable both at the same time.
#define VL53L1X_XSHUT_GPIO 27

esp_err_t init_vl53l1x(void);

#endif // VL53L1X_H_
