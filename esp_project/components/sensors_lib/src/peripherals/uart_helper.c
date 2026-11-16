#include "peripherals/uart_helper.h"
#include "log_lib.h"
#include "esp_timer.h"

static const char *TAG = "uart_helper_peripheral";

esp_err_t uart_bus_init(uart_bus_t *bus, uart_port_t port, int tx_gpio, int rx_gpio,
                         uint32_t baud_rate, size_t rx_buf_size) {
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (bus->initialized) {
        // Already set up for this port; nothing to do.
        return ESP_OK;
    }

    uart_config_t uart_config = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(port, &uart_config);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) configuring UART%d params", esp_err_to_name(err), port);
        return err;
    }

    // UART_PIN_NO_CHANGE lets a TX-only device (e.g. a sensor that never
    // receives commands, like the D500 lidar) skip the RX/TX pin it
    // doesn't use, without this helper needing a separate code path.
    err = uart_set_pin(port, tx_gpio, rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting UART%d pins (tx=%d, rx=%d)", esp_err_to_name(err), port, tx_gpio, rx_gpio);
        return err;
    }

    // rx_buf_size must exceed UART_FIFO_LEN (ESP-IDF requirement for the
    // driver's internal ring buffer); tx_buf_size=0 means transmit calls
    // block until sent instead of queuing, fine for our low-rate writes.
    err = uart_driver_install(port, rx_buf_size, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) installing UART%d driver", esp_err_to_name(err), port);
        return err;
    }

    bus->port = port;
    bus->initialized = true;

    log_msg(TAG, "UART%d initialized (tx: %d, rx: %d, baud: %lu)", port, tx_gpio, rx_gpio, baud_rate);
    return ESP_OK;
}

esp_err_t uart_bus_write(uart_bus_t *bus, const uint8_t *data, size_t len) {
    if (bus == NULL || data == NULL || !bus->initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    int written = uart_write_bytes(bus->port, (const char *)data, len);
    return (written == (int)len) ? ESP_OK : ESP_FAIL;
}

// Returns bytes actually read (0 on timeout with nothing available), or
// a negative esp_err_t-style value on hard driver error - mirrors what
// uart_read_bytes itself returns, so callers doing partial/streaming
// reads (e.g. parsing a continuous D500 frame stream byte-by-byte) get
// the real count rather than an all-or-nothing result.
int uart_bus_read(uart_bus_t *bus, uint8_t *data, size_t len, uint32_t timeout_ms) {
    if (bus == NULL || data == NULL || !bus->initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    return uart_read_bytes(bus->port, data, len, pdMS_TO_TICKS(timeout_ms));
}

// Blocks (up to timeout_ms total, not per-byte) until exactly len bytes
// are collected or the timeout is hit. Use this when you need a complete
// fixed-size frame (e.g. a known header+payload length) and partial data
// is useless - uart_bus_read alone would hand back a short read instead.
esp_err_t uart_bus_read_exact(uart_bus_t *bus, uint8_t *data, size_t len, uint32_t timeout_ms) {
    if (bus == NULL || data == NULL || !bus->initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t total_read = 0;
    int64_t start_us = esp_timer_get_time();

    while (total_read < len) {
        int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
        if ((uint32_t)elapsed_ms >= timeout_ms) {
            return ESP_ERR_TIMEOUT;
        }

        int n = uart_read_bytes(bus->port, data + total_read, len - total_read,
                                 pdMS_TO_TICKS(timeout_ms - elapsed_ms));
        if (n < 0) {
            return ESP_FAIL;
        }
        total_read += (size_t)n;
    }

    return ESP_OK;
}

esp_err_t uart_bus_flush(uart_bus_t *bus) {
    if (bus == NULL || !bus->initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    return uart_flush(bus->port);
}

size_t uart_bus_available(uart_bus_t *bus) {
    if (bus == NULL || !bus->initialized) {
        return 0;
    }
    size_t len = 0;
    uart_get_buffered_data_len(bus->port, &len);
    return len;
}

// UART has no address-ACK like I2C's i2c_bus_probe, so this is the closest
// equivalent: just checks whether any byte shows up within timeout_ms,
// without validating protocol content. Useful at boot to confirm a
// TX-only sensor (D500, GNSS) is actually wired and powered, before
// trusting your real frame parser's silence to mean "broken" rather than
// "nothing plugged in".
bool uart_bus_probe_activity(uart_bus_t *bus, uint32_t timeout_ms) {
    if (bus == NULL || !bus->initialized) {
        return false;
    }
    uint8_t dummy;
    int n = uart_read_bytes(bus->port, &dummy, 1, pdMS_TO_TICKS(timeout_ms));
    return n > 0;
}