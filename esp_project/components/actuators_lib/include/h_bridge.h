#ifndef H_BRIDGE_H_
#define H_BRIDGE_H_

// BTS7960: DC brushed motor H-Bridge controller

#include <inttypes.h>
#include <stdbool.h>
#include <esp_err.h>
#include "driver/ledc.h"

// --- H-Bridge (BTS7960) hardware configuration ---
#define BTS_TIMER           LEDC_TIMER_0
#if CONFIG_IDF_TARGET_ESP32
#define BTS_SPEED_MODE      LEDC_HIGH_SPEED_MODE
#else
#define BTS_SPEED_MODE      LEDC_LOW_SPEED_MODE
#endif
#define BTS_GPIO_FWD        41
#define BTS_CHANNEL_FWD     LEDC_CHANNEL_0
#define BTS_GPIO_BWD        42
#define BTS_CHANNEL_BWD     LEDC_CHANNEL_1
#define BTS_FREQ            20000 // max BTS7960 PWM frequency: 25kHz
#define BTS_RESOLUTION      LEDC_TIMER_11_BIT

extern ledc_timer_config_t ledc_timer_bts;

/**
 * Configure the BTS7960 LEDC timer and both (forward/backward) channels,
 * and start the periodic ramp-control timer that drives the motor curve.
 */
esp_err_t init_bts(void);

/**
 * Stop the ramp-control timer and pause/reset the BTS7960 LEDC timer.
 */
esp_err_t close_bts(void);

/**
 * Apply a motor command in the [-1000, 1000] range (per-mille of full duty).
 * Sign convention: this function inverts the given percent internally
 * (see implementation) to match the physical wiring of forward/backward pins.
 * Ignored while an emergency braking sequence is active (see force_motor_stop()).
 * Also refuses to drive into a direction currently blocked by an obstacle
 * sensor (see sensors_lib's get_front_blocked() / get_rear_blocked()).
 *
 * @param motor_percent desired motor command, clamped to [-1000, 1000]
 */
esp_err_t ledc_motor(int16_t motor_percent);

/**
 * Read the current (ramped) motor value, not the target.
 *
 * @param motor output: current motor value in [-1000, 1000]
 */
esp_err_t get_motor_percent(int16_t *motor);

/**
 * Read the sign of the last non-zero motor direction applied.
 *
 * @param sign output: true if the last direction was forward (positive)
 */
esp_err_t get_last_motor_sign_positive(bool *sign);

/**
 * Set the motor target directly, bypassing ledc_motor()'s deadzone and
 * obstacle-blocking checks. Used internally by the braking sequence.
 * Exposed for cases where a raw target write is explicitly needed.
 *
 * @param motor target motor value, clamped to [-1000, 1000]
 */
esp_err_t set_motor_percent(int16_t motor);

/**
 * Trigger an emergency braking sequence: reverses the motor direction at
 * full duty (plug braking) until the vehicle's real speed (measured via
 * the wheel encoder pulse count) confirms a stop, or until a computed
 * timeout elapses as a safety net. While active, ledc_motor() and
 * set_motor_percent() are ignored.
 *
 * @return ESP_OK if braking was triggered, ESP_ERR_INVALID_STATE if a
 *         braking sequence is already in progress or pulse count is unavailable
 */
esp_err_t force_motor_stop(void);

/**
 * Update the drive profile (ramp curve type + accel/decel parameters).
 *
 * @param buf raw command buffer: [0]=curve_type, [1]=accel_param, [2]=decel_param
 * @param len buffer length, must be at least 3 bytes
 */
esp_err_t apply_config(uint8_t *buf, uint8_t len);

/**
 * Enable or disable obstacle-based direction blocking (front/rear HC-SR04).
 * When disabled, ledc_motor() no longer refuses commands toward a detected
 * obstacle direction.
 */
esp_err_t activate_hc_blocking(bool active);

/**
 * Read whether obstacle-based direction blocking is currently active.
 */
esp_err_t get_active_hc_blocking(bool *active);

#endif // H_BRIDGE_H_
