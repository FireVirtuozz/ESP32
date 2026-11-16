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
#define VL53L1X_XSHUT_GPIO 47

// --- Bench calibration ---
// Set to 1, place the target, flash once. The offset/xtalk found are
// stored in NVS (namespace "vl53l1x_cal") and get reapplied on every
// normal boot afterwards. Set back to 0 and reflash once you've confirmed
// the values in the logs - they persist in NVS across reflashes as long
// as you don't run `idf.py erase-flash`.
#define VL53L1X_CALIBRATE_ON_BOOT      0
#define VL53L1X_CALIB_OFFSET_TARGET_MM 140   // 17%-grey target, ST recommended
#define VL53L1X_CALIB_XTALK_TARGET_MM  600   // cover-glass inflection point, adjust to your housing

// Seconds to wait between offset and xtalk calibration - gives you time to
// physically move the target from VL53L1X_CALIB_OFFSET_TARGET_MM to
// VL53L1X_CALIB_XTALK_TARGET_MM. Watch the countdown in the serial monitor.
#define VL53L1X_CALIB_MOVE_DELAY_S     30

esp_err_t init_vl53l1x(void);

#endif // VL53L1X_H_