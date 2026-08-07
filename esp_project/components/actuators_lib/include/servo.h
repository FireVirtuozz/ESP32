#ifndef SERVO_H_
#define SERVO_H_

// MG996R: standard hobby servo motor (steering)

#include <inttypes.h>
#include <esp_err.h>
#include "driver/ledc.h"

#define MG_TIMER            LEDC_TIMER_1
#if CONFIG_IDF_TARGET_ESP32
#define MG_SPEED_MODE       LEDC_HIGH_SPEED_MODE
#else
#define MG_SPEED_MODE       LEDC_LOW_SPEED_MODE
#endif
#define MG_GPIO             (7) // parentheses required to avoid macro-expansion pitfalls
#define MG_CHANNEL          LEDC_CHANNEL_2
#define MG_FREQ             50 // 50Hz, required by the MG996R
#if CONFIG_IDF_TARGET_ESP32
#define MG_RESOLUTION       LEDC_TIMER_15_BIT
#else
#define MG_RESOLUTION       LEDC_TIMER_14_BIT
#endif

#if CONFIG_IDF_TARGET_ESP32
#define MIN_SERVO_DUTY 1638
#define MAX_SERVO_DUTY 3277
#else
#define MIN_SERVO_DUTY 969
#define MAX_SERVO_DUTY 1489
#endif

extern ledc_timer_config_t ledc_timer_mg;

/**
 * Configure the MG996R LEDC timer and channel.
 */
esp_err_t init_servo(void);

/**
 * Pause and reset the MG996R LEDC timer.
 */
esp_err_t close_servo(void);

/**
 * Apply a steering angle, clamped to [0, 180] degrees.
 */
esp_err_t ledc_angle(int16_t angle);

/**
 * Read the last angle applied.
 */
esp_err_t get_servo_angle(uint8_t *angle);

#endif // SERVO_H_
