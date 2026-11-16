#ifndef VL53L1X_PLATFORM_GLUE_H
#define VL53L1X_PLATFORM_GLUE_H

#include <stdint.h>
#include "peripherals/i2c_helper.h"

#ifdef __cplusplus
extern "C" {
#endif

// The ST ULD API only knows about an opaque `uint16_t dev`, it has no
// concept of your i2c_master_dev_handle_t. This glue keeps a tiny table
// so vl53l1_platform.c can turn `dev` back into the right handle.
// Bump this if you add a second VL53L1X while splitting sensors_lib.c.
#define VL53L1X_PLATFORM_MAX_DEVICES 2

// Call once, right after i2c_bus_add_device(), before any VL53L1X_* call.
void vl53l1x_platform_bind(uint16_t dev_id, i2c_master_dev_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // VL53L1X_PLATFORM_GLUE_H