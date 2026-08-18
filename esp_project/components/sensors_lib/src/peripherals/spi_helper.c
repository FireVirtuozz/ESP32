#include "peripherals/spi_helper.h"
#include "log_lib.h"

static const char *TAG = "spi_helper_peripheral";

static bool bus_initialized[SPI_HOST_MAX] = { false };

esp_err_t spi_bus_init(spi_host_device_t host, int miso, int mosi, int clk) {
    if (bus_initialized[host]) {
        return ESP_OK;
    }

    spi_bus_config_t bus_config = {
        .miso_io_num = miso,
        .mosi_io_num = mosi,
        .sclk_io_num = clk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    esp_err_t err = spi_bus_initialize(host, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) initializing SPI bus on host %d", esp_err_to_name(err), host);
        return err;
    }

    bus_initialized[host] = true;
    log_msg(TAG, "SPI bus initialized on host %d (MISO: %d, MOSI: %d, CLK: %d)", host, miso, mosi, clk);
    return ESP_OK;
}

esp_err_t spi_helper_add_device(spi_host_device_t host, int cs_gpio, int clock_hz, uint8_t mode,
    spi_device_handle_t *out_handle) {
    if (out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    spi_device_interface_config_t dev_config = {
        .clock_speed_hz = clock_hz,
        .mode = mode,
        .spics_io_num = cs_gpio,
        .queue_size = 1,
    };

    // Calls the ESP-IDF driver function of the same base name; our wrapper
    // is named spi_helper_add_device precisely to avoid shadowing/recursing
    // into itself, since C has no namespaces to disambiguate identical names.
    esp_err_t err = spi_bus_add_device(host, &dev_config, out_handle);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding SPI device (CS: %d)", esp_err_to_name(err), cs_gpio);
        return err;
    }

    return ESP_OK;
}

esp_err_t spi_bus_transfer(spi_device_handle_t dev, const uint8_t *tx, uint8_t *rx, size_t len) {
    spi_transaction_t trans = {
        .length = len * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    return spi_device_transmit(dev, &trans);
}
