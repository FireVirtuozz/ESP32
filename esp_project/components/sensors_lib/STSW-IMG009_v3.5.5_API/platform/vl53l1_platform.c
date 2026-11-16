/**
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
// Implements the 9 callbacks declared in API/platform/vl53l1_platform.h
// (ST's VL53L1X ULD API v3.5.5), on top of your existing i2c_bus_* helpers.
//
// ASSUMPTION (can't see i2c_helper.h from here, inferred from your old
// vl53l1x_sensor.c call sites — check these signatures match):
//   esp_err_t i2c_bus_write_reg16addr(i2c_master_dev_handle_t, uint16_t reg, uint8_t data);
//   esp_err_t i2c_bus_write_block16addr(i2c_master_dev_handle_t, uint16_t reg, const uint8_t *data, size_t len);
//   esp_err_t i2c_bus_read_reg16addr(i2c_master_dev_handle_t, uint16_t reg, uint8_t *data, size_t len);

#include "vl53l1_platform.h"
#include "vl53l1x_platform_glue.h"
#include "peripherals/i2c_helper.h"
#include "log_lib.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>

static const char *TAG = "vl53l1x_platform";

static i2c_master_dev_handle_t s_devices[VL53L1X_PLATFORM_MAX_DEVICES];
static bool s_bound[VL53L1X_PLATFORM_MAX_DEVICES];

void vl53l1x_platform_bind(uint16_t dev_id, i2c_master_dev_handle_t handle) {
    if (dev_id >= VL53L1X_PLATFORM_MAX_DEVICES) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "dev_id %u out of range", dev_id);
        return;
    }
    s_devices[dev_id] = handle;
    s_bound[dev_id] = true;
}

static i2c_master_dev_handle_t get_handle(uint16_t dev_id) {
    if (dev_id >= VL53L1X_PLATFORM_MAX_DEVICES || !s_bound[dev_id]) {
        return NULL;
    }
    return s_devices[dev_id];
}

int8_t VL53L1_WriteMulti(uint16_t dev, uint16_t index, uint8_t *pdata, uint32_t count) {
    i2c_master_dev_handle_t h = get_handle(dev);
    if (!h) return -1;
    return i2c_bus_write_block16addr(h, index, pdata, count) == ESP_OK ? 0 : -1;
}

int8_t VL53L1_ReadMulti(uint16_t dev, uint16_t index, uint8_t *pdata, uint32_t count) {
    i2c_master_dev_handle_t h = get_handle(dev);
    if (!h) return -1;
    return i2c_bus_read_reg16addr(h, index, pdata, count) == ESP_OK ? 0 : -1;
}

int8_t VL53L1_WrByte(uint16_t dev, uint16_t index, uint8_t data) {
    i2c_master_dev_handle_t h = get_handle(dev);
    if (!h) return -1;
    return i2c_bus_write_reg16addr(h, index, data) == ESP_OK ? 0 : -1;
}

int8_t VL53L1_WrWord(uint16_t dev, uint16_t index, uint16_t data) {
    i2c_master_dev_handle_t h = get_handle(dev);
    if (!h) return -1;
    // Registers are big-endian on this sensor (see original driver's
    // manual dist_raw[0]<<8|dist_raw[1] reconstruction).
    uint8_t buf[2] = { (uint8_t)(data >> 8), (uint8_t)(data & 0xFF) };
    return i2c_bus_write_block16addr(h, index, buf, sizeof(buf)) == ESP_OK ? 0 : -1;
}

int8_t VL53L1_WrDWord(uint16_t dev, uint16_t index, uint32_t data) {
    i2c_master_dev_handle_t h = get_handle(dev);
    if (!h) return -1;
    uint8_t buf[4] = {
        (uint8_t)(data >> 24), (uint8_t)(data >> 16),
        (uint8_t)(data >> 8),  (uint8_t)(data & 0xFF)
    };
    return i2c_bus_write_block16addr(h, index, buf, sizeof(buf)) == ESP_OK ? 0 : -1;
}

int8_t VL53L1_RdByte(uint16_t dev, uint16_t index, uint8_t *pdata) {
    i2c_master_dev_handle_t h = get_handle(dev);
    if (!h) return -1;
    return i2c_bus_read_reg16addr(h, index, pdata, 1) == ESP_OK ? 0 : -1;
}

int8_t VL53L1_RdWord(uint16_t dev, uint16_t index, uint16_t *pdata) {
    i2c_master_dev_handle_t h = get_handle(dev);
    if (!h) return -1;
    uint8_t buf[2];
    if (i2c_bus_read_reg16addr(h, index, buf, sizeof(buf)) != ESP_OK) return -1;
    *pdata = ((uint16_t)buf[0] << 8) | buf[1];
    return 0;
}

int8_t VL53L1_RdDWord(uint16_t dev, uint16_t index, uint32_t *pdata) {
    i2c_master_dev_handle_t h = get_handle(dev);
    if (!h) return -1;
    uint8_t buf[4];
    if (i2c_bus_read_reg16addr(h, index, buf, sizeof(buf)) != ESP_OK) return -1;
    *pdata = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16)
           | ((uint32_t)buf[2] << 8)  | buf[3];
    return 0;
}

int8_t VL53L1_WaitMs(uint16_t dev, int32_t wait_ms) {
    (void)dev;
    vTaskDelay(pdMS_TO_TICKS(wait_ms));
    return 0;
}