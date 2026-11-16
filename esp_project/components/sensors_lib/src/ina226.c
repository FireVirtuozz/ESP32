#include "ina226.h"
#include "sensors_lib.h"
#include "peripherals/i2c_helper.h"
#include "log_lib.h"
#include <string.h>

#if CONFIG_USE_UDPLIB
#include "udp_lib.h"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "ina226_sensor";

#if CONFIG_USE_INA226

#define REG_CONFIG  0x00
#define REG_SHUNT_V 0x01
#define REG_BUS_V   0x02
#define REG_POWER   0x03
#define REG_CURRENT 0x04
#define REG_CALIB   0x05

// Wire payload layout preserved exactly from the original firmware:
// [bus: i16][current: u16][power: u16][shunt: i16], all raw register values.
typedef struct {
    int16_t shunt;
    int16_t bus;
    uint16_t current;
    uint16_t power;
} ina226_info_t;

static i2c_master_dev_handle_t dev;

static void serialize_ina226(const ina226_info_t *info, uint8_t *buf) {
    uint16_t len = HEADER_SENSOR_SIZE;
    memcpy(&buf[len], &info->bus, sizeof(int16_t));
    len += sizeof(int16_t);
    memcpy(&buf[len], &info->current, sizeof(uint16_t));
    len += sizeof(uint16_t);
    memcpy(&buf[len], &info->power, sizeof(uint16_t));
    len += sizeof(uint16_t);
    memcpy(&buf[len], &info->shunt, sizeof(int16_t));
}

static esp_err_t read_reg_be(uint8_t reg, uint16_t *value) {
    uint8_t raw[2];
    esp_err_t err = i2c_bus_read_reg8(dev, reg, raw, sizeof(raw));
    if (err != ESP_OK) return err;
    *value = ((uint16_t)raw[0] << 8) | raw[1];
    return ESP_OK;
}

static void ina226_task(void *params) {
    (void)params;

    if (i2c_bus_add_device(INA226_I2C_ADDR, 400000, &dev) != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    // Config: reset bit set (matches original: cfg_frame = 1<<15, rest 0).
    i2c_bus_write_reg16(dev, REG_CONFIG, 0x8000);
    vTaskDelay(pdMS_TO_TICKS(2));

    // Calibration: fixed value 2048, matching the original's comment-derived
    // constant (CAL = 0.00512 / (Current_LSB * Rshunt), Rshunt=0.1ohm,
    // Current_LSB=25uA -> CAL=2048) rather than a recomputed formula.
    i2c_bus_write_reg16(dev, REG_CALIB, 2048);

    log_msg(TAG, "INA226 initialized at address 0x%02X", INA226_I2C_ADDR);

    while (true) {
        ina226_info_t info = {0};
        bool ok = read_reg_be(REG_SHUNT_V, (uint16_t *)&info.shunt) == ESP_OK &&
                  read_reg_be(REG_BUS_V, (uint16_t *)&info.bus) == ESP_OK &&
                  read_reg_be(REG_POWER, &info.power) == ESP_OK &&
                  read_reg_be(REG_CURRENT, &info.current) == ESP_OK;

        if (ok) {
            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_INA226;
            uint8_t buf[HEADER_SENSOR_SIZE + 2 * sizeof(int16_t) + 2 * sizeof(uint16_t)];
            serialize_header(&header, buf);
            serialize_ina226(&info, buf);

#if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
#endif
        }
        vTaskDelay(pdMS_TO_TICKS(70)); // matches original INA_PERIOD
    }
}

esp_err_t init_ina226(void) {
    return xTaskCreate(ina226_task, "ina226_task", 3072, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

#else // !CONFIG_USE_INA226

esp_err_t init_ina226(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_INA226
