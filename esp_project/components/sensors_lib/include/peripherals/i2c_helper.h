#ifndef PERIPHERALS_I2C_HELPER_H_
#define PERIPHERALS_I2C_HELPER_H_

#include <inttypes.h>
#include <esp_err.h>
#include "driver/i2c_master.h"

// Generic building blocks for I2C sensors. A single I2C bus is shared by
// every I2C sensor in this project (INA226, BMP280, MPU9250, VL53L1X,
// AS5600); i2c_bus_init() is idempotent so each sensor can call it without
// coordinating init order, and every sensor gets its own device handle on
// that shared bus via i2c_bus_add_device().

/**
 * Initialize the shared I2C master bus (SDA/SCL pins, defined once for the
 * whole project). Safe to call multiple times: only the first call actually
 * creates the bus, subsequent calls are no-ops returning ESP_OK.
 */
esp_err_t i2c_bus_init(void);

/**
 * Add a device on the shared I2C bus and get a handle to talk to it.
 *
 * @param device_addr  7-bit I2C address of the device
 * @param scl_speed_hz bus speed to use for this device (e.g. 100000 or 400000)
 * @param out_handle   output: device handle
 */
esp_err_t i2c_bus_add_device(uint16_t device_addr, uint32_t scl_speed_hz, i2c_master_dev_handle_t *out_handle);

/**
 * Write a single byte to an 8-bit register.
 */
esp_err_t i2c_bus_write_reg8(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value);

/**
 * Read `len` bytes starting at an 8-bit register.
 */
esp_err_t i2c_bus_read_reg8(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t len);

/**
 * Write a big-endian 16-bit value to an 8-bit register (common on sensors
 * like INA226 whose registers are all 16-bit).
 */
esp_err_t i2c_bus_write_reg16(i2c_master_dev_handle_t dev, uint8_t reg, uint16_t value);

/**
 * Read a big-endian 16-bit value from an 8-bit register.
 */
esp_err_t i2c_bus_read_reg16(i2c_master_dev_handle_t dev, uint8_t reg, int16_t *value);

/**
 * Write a single byte to a 16-bit register address (some sensors, like
 * VL53L1X, address their registers with 16 bits instead of 8).
 */
esp_err_t i2c_bus_write_reg16addr(i2c_master_dev_handle_t dev, uint16_t reg, uint8_t value);

/**
 * Write an arbitrary-length buffer starting at a 16-bit register address in
 * one transaction (e.g. loading a sensor's multi-byte factory config table).
 * `data` should NOT include the register address; it is prepended internally.
 */
esp_err_t i2c_bus_write_block16addr(i2c_master_dev_handle_t dev, uint16_t reg, const uint8_t *data, size_t len);

/**
 * Read `len` bytes starting at a 16-bit register address.
 */
esp_err_t i2c_bus_read_reg16addr(i2c_master_dev_handle_t dev, uint16_t reg, uint8_t *data, size_t len);

#endif // PERIPHERALS_I2C_HELPER_H_
