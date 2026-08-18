#include "peripherals/i2c_helper.h"
#include "log_lib.h"
#include <string.h>

static const char *TAG = "i2c_helper_peripheral";

// Shared I2C bus pins, used by every I2C sensor in this project.
#define I2C_SCL_GPIO 25
#define I2C_SDA_GPIO 26
#define I2C_PORT_NUM I2C_NUM_0

static i2c_master_bus_handle_t bus_handle = NULL;

esp_err_t i2c_bus_init(void) {
    if (bus_handle != NULL) {
        // Already initialized by a previous sensor; nothing to do.
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_PORT_NUM,
        .scl_io_num = I2C_SCL_GPIO,
        .sda_io_num = I2C_SDA_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_config, &bus_handle);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) creating shared I2C bus", esp_err_to_name(err));
        return err;
    }

    log_msg(TAG, "Shared I2C bus initialized (SCL: %d, SDA: %d)", I2C_SCL_GPIO, I2C_SDA_GPIO);
    return ESP_OK;
}

esp_err_t i2c_bus_add_device(uint16_t device_addr, uint32_t scl_speed_hz, i2c_master_dev_handle_t *out_handle) {
    if (out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = i2c_bus_init();
    if (err != ESP_OK) {
        return err;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device_addr,
        .scl_speed_hz = scl_speed_hz,
    };

    err = i2c_master_bus_add_device(bus_handle, &dev_config, out_handle);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding I2C device 0x%02X", esp_err_to_name(err), device_addr);
        return err;
    }

    return ESP_OK;
}

esp_err_t i2c_bus_write_reg8(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value) {
    uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(dev, buf, sizeof(buf), -1);
}

esp_err_t i2c_bus_read_reg8(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t len) {
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(dev, &reg, 1, data, len, -1);
}

esp_err_t i2c_bus_write_reg16(i2c_master_dev_handle_t dev, uint8_t reg, uint16_t value) {
    uint8_t buf[3] = { reg, (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
    return i2c_master_transmit(dev, buf, sizeof(buf), -1);
}

esp_err_t i2c_bus_read_reg16(i2c_master_dev_handle_t dev, uint8_t reg, int16_t *value) {
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t raw[2];
    esp_err_t err = i2c_bus_read_reg8(dev, reg, raw, 2);
    if (err != ESP_OK) {
        return err;
    }
    *value = (int16_t)((raw[0] << 8) | raw[1]);
    return ESP_OK;
}

esp_err_t i2c_bus_write_reg16addr(i2c_master_dev_handle_t dev, uint16_t reg, uint8_t value) {
    uint8_t buf[3] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), value };
    return i2c_master_transmit(dev, buf, sizeof(buf), -1);
}

esp_err_t i2c_bus_read_reg16addr(i2c_master_dev_handle_t dev, uint16_t reg, uint8_t *data, size_t len) {
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t addr[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    return i2c_master_transmit_receive(dev, addr, sizeof(addr), data, len, -1);
}

esp_err_t i2c_bus_write_block16addr(i2c_master_dev_handle_t dev, uint16_t reg, const uint8_t *data, size_t len) {
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // Small fixed-size stack buffer covers every sensor config table in
    // this project (largest is 91 bytes); reject anything unexpectedly
    // larger rather than silently truncating or growing the stack unbounded.
    #define I2C_BLOCK_WRITE_MAX 256
    if (len > I2C_BLOCK_WRITE_MAX - 2) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t buf[I2C_BLOCK_WRITE_MAX];
    buf[0] = (uint8_t)(reg >> 8);
    buf[1] = (uint8_t)(reg & 0xFF);
    memcpy(&buf[2], data, len);

    return i2c_master_transmit(dev, buf, len + 2, -1);
}
