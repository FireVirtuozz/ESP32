#ifndef MPU9250_H_
#define MPU9250_H_

#include <esp_err.h>

// MPU9250/MPU6500: 6-axis IMU (accel + gyro) over I2C.
// Wire format sends RAW register int16_t values (accel with a startup
// offset calibration subtracted, gyro/temp raw), matching the original
// firmware exactly: [ax][ay][az][gx][gy][gz][temp], all i16.
#define MPU9250_I2C_ADDR 0x68

esp_err_t init_mpu9250(void);

#endif // MPU9250_H_
