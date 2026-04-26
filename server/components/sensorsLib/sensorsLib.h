#ifndef SENSORSLIB_H_
#define SENSORSLIB_H_

#define USE_HCSR04 1
#define USE_INA226 1
#define USE_KY003 1
#define USE_MPU9250 1

#include <inttypes.h>
#include <esp_err.h>

#if USE_HCSR04

/**
 * Initialize HC-SR04 sensor gpios
 */
esp_err_t init_hcsr();

/**
 * triggers an echo
 * blocking function for echo's duration
 * @returns duration of echo
 */
int64_t trigger_echo();

#endif

#if USE_INA226

typedef struct ina_info_st {
    int16_t shunt;
    int16_t bus;
    uint16_t current;
    uint16_t power;
} ina_info_t;

/**
 * Initialize INA226 sensor gpios
 * @returns ESP_OK success, others error
 */
esp_err_t init_ina();

/**
 * Getting INA info
 * @returns ESP_OK success, others error 
 */
esp_err_t get_ina_info(ina_info_t *ina_info);

#endif

#if USE_KY003

typedef struct ky_info_st {
    uint64_t signal_count;
    int64_t signal_duration;
} ky_info_t;

/**
 * Initialize KY-003 sensor gpio
 */
esp_err_t init_ky();

/**
 * Get KY's last signal
 * @returns gpio level
 */
ky_info_t* get_signal_info();

#endif

#if USE_MPU9250
typedef struct mpu_info_st {
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t temp_mpu;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
    int32_t temp_bmp;
    int32_t pressure;
} mpu_info_t;

/**
 * Initialize MPU9250 sensor i2c
 */
esp_err_t init_mpu();

/**
 * Getting MPU info
 * @returns ESP_OK success, others error 
 */
esp_err_t get_mpu_info(mpu_info_t *mpu_info);

#endif

esp_err_t start_monitoring_task();

#endif