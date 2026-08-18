#include "mpu9250.h"
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

static const char *TAG = "mpu9250_sensor";

#if CONFIG_USE_MPU9250

#define MPU_PERIOD 50

// Wire payload order preserved exactly from the original firmware:
// accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z, temp — all raw i16.
typedef struct {
    int16_t accel_x, accel_y, accel_z;
    int16_t temp_mpu;
    int16_t gyro_x, gyro_y, gyro_z;
} mpu9250_info_t;

static i2c_master_dev_handle_t dev;
static int16_t accel_offset_x = 0, accel_offset_y = 0, accel_offset_z = 0;

static esp_err_t get_mpu_info(mpu9250_info_t *info) {
    uint8_t buf[14]; // accel(6) + temp(2) + gyro(6)
    esp_err_t err = i2c_bus_read_reg8(dev, 0x3B, buf, sizeof(buf));
    if (err != ESP_OK) return err;

    info->accel_x = (int16_t)(((uint16_t)buf[0] << 8) | buf[1]) - accel_offset_x;
    info->accel_y = (int16_t)(((uint16_t)buf[2] << 8) | buf[3]) - accel_offset_y;
    info->accel_z = (int16_t)(((uint16_t)buf[4] << 8) | buf[5]) - accel_offset_z;
    info->temp_mpu = ((uint16_t)buf[6] << 8) | buf[7];
    info->gyro_x = ((uint16_t)buf[8] << 8) | buf[9];
    info->gyro_y = ((uint16_t)buf[10] << 8) | buf[11];
    info->gyro_z = ((uint16_t)buf[12] << 8) | buf[13];
    return ESP_OK;
}

/**
 * Average 200 samples at rest to get a zero-offset for the accelerometer
 * (gyro/temp are sent raw, uncalibrated). Blocks ~1s at startup — the car
 * must be still while this runs, matching the original firmware.
 */
static esp_err_t calibrate_accel_offset(void) {
    const int N = 200;
    int32_t sum_x = 0, sum_y = 0, sum_z = 0;
    mpu9250_info_t sample;

    log_msg(TAG, "Calibrating accel offset, keep car still...");
    for (int i = 0; i < N; i++) {
        esp_err_t err = get_mpu_info(&sample);
        if (err != ESP_OK) return err;
        sum_x += sample.accel_x;
        sum_y += sample.accel_y;
        sum_z += sample.accel_z;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    accel_offset_x = (int16_t)(sum_x / N);
    accel_offset_y = (int16_t)(sum_y / N);
    accel_offset_z = (int16_t)(sum_z / N);
    log_msg(TAG, "Offsets: x=%d y=%d z=%d", accel_offset_x, accel_offset_y, accel_offset_z);
    return ESP_OK;
}

static void serialize_mpu9250(const mpu9250_info_t *info, uint8_t *buf) {
    uint16_t len = HEADER_SENSOR_SIZE;
    memcpy(&buf[len], &info->accel_x, sizeof(int16_t)); len += sizeof(int16_t);
    memcpy(&buf[len], &info->accel_y, sizeof(int16_t)); len += sizeof(int16_t);
    memcpy(&buf[len], &info->accel_z, sizeof(int16_t)); len += sizeof(int16_t);
    memcpy(&buf[len], &info->gyro_x, sizeof(int16_t));  len += sizeof(int16_t);
    memcpy(&buf[len], &info->gyro_y, sizeof(int16_t));  len += sizeof(int16_t);
    memcpy(&buf[len], &info->gyro_z, sizeof(int16_t));  len += sizeof(int16_t);
    memcpy(&buf[len], &info->temp_mpu, sizeof(int16_t));
}

static void mpu9250_task(void *params) {
    (void)params;

    if (i2c_bus_add_device(MPU9250_I2C_ADDR, 400000, &dev) != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    i2c_bus_write_reg8(dev, 0x6B, 0x80); // reset
    vTaskDelay(pdMS_TO_TICKS(100));
    i2c_bus_write_reg8(dev, 0x6B, 0x01); // wake, use gyro clock
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t who = 0;
    i2c_bus_read_reg8(dev, 0x75, &who, 1);
    log_msg(TAG, "WHO_AM_I: 0x%02X", who);

    i2c_bus_write_reg8(dev, 0x1A, 0x03); // CONFIG: gyro DLPF ~41Hz
    i2c_bus_write_reg8(dev, 0x1D, 0x03); // ACCEL_CONFIG_2: accel DLPF ~44.8Hz
    vTaskDelay(pdMS_TO_TICKS(100));

    log_msg(TAG, "MPU9250 initialized at address 0x%02X", MPU9250_I2C_ADDR);

    calibrate_accel_offset();

    while (true) {
        mpu9250_info_t info;
        if (get_mpu_info(&info) == ESP_OK) {
            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_MPU9250;
            uint8_t buf[HEADER_SENSOR_SIZE + 7 * sizeof(int16_t)];
            serialize_header(&header, buf);
            serialize_mpu9250(&info, buf);

#if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
#endif
        }
        vTaskDelay(pdMS_TO_TICKS(MPU_PERIOD));
    }
}

esp_err_t init_mpu9250(void) {
    return xTaskCreate(mpu9250_task, "mpu9250_task", 3072, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

#else // !CONFIG_USE_MPU9250

esp_err_t init_mpu9250(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_MPU9250
