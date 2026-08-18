#include "bmp280.h"
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

static const char *TAG = "bmp280_sensor";

#if CONFIG_USE_BMP280

#define BMP_PERIOD 50

// Wire payload: [pressure: i32][temperature: i32], in that exact order.
typedef struct {
    int32_t pressure;
    int32_t temperature;
} bmp280_info_t;

static i2c_master_dev_handle_t dev;

static uint16_t dig_T1;
static int16_t dig_T2, dig_T3;
static uint16_t dig_P1;
static int16_t dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
static int32_t t_fine;

static esp_err_t read_calibration(void) {
    uint8_t buf_t[6];
    esp_err_t err = i2c_bus_read_reg8(dev, 0x88, buf_t, sizeof(buf_t));
    if (err != ESP_OK) return err;
    dig_T1 = buf_t[0] | (buf_t[1] << 8);
    dig_T2 = (int16_t)(buf_t[2] | (buf_t[3] << 8));
    dig_T3 = (int16_t)(buf_t[4] | (buf_t[5] << 8));

    uint8_t buf_p[18];
    err = i2c_bus_read_reg8(dev, 0x8E, buf_p, sizeof(buf_p));
    if (err != ESP_OK) return err;
    dig_P1 = buf_p[0] | ((uint16_t)buf_p[1] << 8);
    dig_P2 = (int16_t)(buf_p[2] | ((uint16_t)buf_p[3] << 8));
    dig_P3 = (int16_t)(buf_p[4] | ((uint16_t)buf_p[5] << 8));
    dig_P4 = (int16_t)(buf_p[6] | ((uint16_t)buf_p[7] << 8));
    dig_P5 = (int16_t)(buf_p[8] | ((uint16_t)buf_p[9] << 8));
    dig_P6 = (int16_t)(buf_p[10] | ((uint16_t)buf_p[11] << 8));
    dig_P7 = (int16_t)(buf_p[12] | ((uint16_t)buf_p[13] << 8));
    dig_P8 = (int16_t)(buf_p[14] | ((uint16_t)buf_p[15] << 8));
    dig_P9 = (int16_t)(buf_p[16] | ((uint16_t)buf_p[17] << 8));
    return ESP_OK;
}

// Bosch reference fixed-point formulas (BST-BMP280-DS001), int32 variant.
static esp_err_t convert_temperature(int32_t *raw_temp) {
    int32_t v1, v2, T;
    v1 = (((*raw_temp >> 3) - ((int32_t)dig_T1 << 1)) * (int32_t)dig_T2) >> 11;
    v2 = (((((*raw_temp >> 4) - (int32_t)dig_T1) *
        ((*raw_temp >> 4) - (int32_t)dig_T1)) >> 12) * (int32_t)dig_T3) >> 14;
    t_fine = v1 + v2;
    T = (t_fine * 5 + 128) >> 8;
    *raw_temp = T;
    return ESP_OK;
}

static esp_err_t convert_pressure(int32_t *raw_pressure) {
    int32_t var1, var2;
    uint32_t p;

    var1 = ((int32_t)t_fine >> 1) - (int32_t)64000;
    var2 = (((var1 >> 2) * (var1 >> 2)) >> 11) * (int32_t)dig_P6;
    var2 = var2 + ((var1 * (int32_t)dig_P5) << 1);
    var2 = (var2 >> 2) + ((int32_t)dig_P4 << 16);
    var1 = (((int32_t)dig_P3 * (((var1 >> 2) * (var1 >> 2)) >> 13) >> 3)
            + (((int32_t)dig_P2 * var1) >> 1)) >> 18;
    var1 = ((32768 + var1) * (int32_t)dig_P1) >> 15;

    if (var1 == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    p = (uint32_t)((int32_t)1048576 - *raw_pressure);
    p = (p - (uint32_t)(var2 >> 12)) * 3125;

    if (p < 0x80000000U) {
        p = (p << 1) / (uint32_t)var1;
    } else {
        p = (p / (uint32_t)var1) * 2;
    }

    var1 = ((int32_t)dig_P9 * (int32_t)(((p >> 3) * (p >> 3)) >> 13)) >> 12;
    var2 = ((int32_t)(p >> 2) * (int32_t)dig_P8) >> 13;
    p = (uint32_t)((int32_t)p + ((var1 + var2 + dig_P7) >> 4));

    *raw_pressure = (int32_t)p;
    return ESP_OK;
}

static void serialize_bmp280(const bmp280_info_t *info, uint8_t *buf) {
    uint16_t len = HEADER_SENSOR_SIZE;
    memcpy(&buf[len], &info->pressure, sizeof(int32_t));
    len += sizeof(int32_t);
    memcpy(&buf[len], &info->temperature, sizeof(int32_t));
}

static void bmp280_task(void *params) {
    (void)params;

    if (i2c_bus_add_device(BMP280_I2C_ADDR, 100000, &dev) != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    i2c_bus_write_reg8(dev, 0xE0, 0xB6); // soft reset
    vTaskDelay(pdMS_TO_TICKS(10));

    // ctrl_meas: normal mode (bits0-1) + pressure oversample x1 (bit2) +
    // temperature oversample x1 (bit5) = 0x27.
    i2c_bus_write_reg8(dev, 0xF4, 0x27);

    if (read_calibration() != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    log_msg(TAG, "BMP280 initialized at address 0x%02X", BMP280_I2C_ADDR);

    while (true) {
        uint8_t raw[6];
        if (i2c_bus_read_reg8(dev, 0xF7, raw, sizeof(raw)) == ESP_OK) {
            bmp280_info_t info;
            info.pressure = (raw[0] << 12) | (raw[1] << 4) | (raw[2] >> 4);
            info.temperature = (raw[3] << 12) | (raw[4] << 4) | (raw[5] >> 4);

            if (convert_temperature(&info.temperature) == ESP_OK &&
                convert_pressure(&info.pressure) == ESP_OK) {

                header_sensor_t header = {0};
                header.esp_id = (uint8_t)CONFIG_ESP_ID;
                header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
                header.type = SENSOR_TYPE_BMP;
                uint8_t buf[HEADER_SENSOR_SIZE + 2 * sizeof(int32_t)];
                serialize_header(&header, buf);
                serialize_bmp280(&info, buf);

#if CONFIG_USE_UDPLIB
                send_udp_sensor(buf, sizeof(buf));
#endif
            }
        }
        vTaskDelay(pdMS_TO_TICKS(BMP_PERIOD));
    }
}

esp_err_t init_bmp280(void) {
    return xTaskCreate(bmp280_task, "bmp280_task", 3072, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

#else // !CONFIG_USE_BMP280

esp_err_t init_bmp280(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_BMP280
