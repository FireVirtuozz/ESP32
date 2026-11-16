#ifndef PERIPHERALS_SPI_HELPER_H_
#define PERIPHERALS_SPI_HELPER_H_

#include <inttypes.h>
#include <esp_err.h>
#include "driver/spi_master.h"

// Generic building blocks for SPI sensors. Only one SPI sensor exists in
// this project today (RC522), but the bus/device split mirrors i2c_helper.h
// so a second SPI sensor can share the bus the same way.

/**
 * Initialize a SPI bus (MISO/MOSI/CLK pins) on the given host.
 * Safe to call multiple times per host: only the first call for a given
 * host actually initializes it.
 */
esp_err_t spi_bus_init(spi_host_device_t host, int miso, int mosi, int clk);

/**
 * Add a device on an already-initialized SPI bus.
 *
 * @param host       SPI host the bus was initialized on
 * @param cs_gpio    this device's chip-select pin
 * @param clock_hz   SPI clock speed for this device
 * @param mode       SPI mode (0-3)
 * @param out_handle output: device handle
 */
esp_err_t spi_helper_add_device(spi_host_device_t host, int cs_gpio, int clock_hz, uint8_t mode,
    spi_device_handle_t *out_handle);

/**
 * Perform a full-duplex SPI transfer of `len` bytes.
 * `tx` and/or `rx` may be NULL if only one direction is needed.
 */
esp_err_t spi_bus_transfer(spi_device_handle_t dev, const uint8_t *tx, uint8_t *rx, size_t len);

#endif // PERIPHERALS_SPI_HELPER_H_
