// peripherals/uart_helper.h
#pragma once
#include "driver/uart.h"
#include "esp_err.h"

typedef struct {
    uart_port_t port;
    bool initialized;
} uart_bus_t;

esp_err_t uart_bus_init(uart_bus_t *bus, uart_port_t port, int tx_gpio, int rx_gpio,
                         uint32_t baud_rate, size_t rx_buf_size);

esp_err_t uart_bus_write(uart_bus_t *bus, const uint8_t *data, size_t len);
int       uart_bus_read(uart_bus_t *bus, uint8_t *data, size_t len, uint32_t timeout_ms);
esp_err_t uart_bus_read_exact(uart_bus_t *bus, uint8_t *data, size_t len, uint32_t timeout_ms);

esp_err_t uart_bus_flush(uart_bus_t *bus);
size_t    uart_bus_available(uart_bus_t *bus);

// Diagnostic: checks if ANY bytes arrive within timeout_ms, without
// consuming the real protocol logic - the UART equivalent of "does
// anything answer", since there's no address-ACK mechanism like I2C.
bool uart_bus_probe_activity(uart_bus_t *bus, uint32_t timeout_ms);