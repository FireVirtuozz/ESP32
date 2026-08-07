#ifndef SIMPLE_LED_H_
#define SIMPLE_LED_H_

#include <esp_err.h>

// Simple GPIO on/off LED (built-in board LED, single color, no PWM)

/**
 * Initialize the simple LED: create its mutex, reset and configure its GPIO,
 * and restore its last saved state from NVS if CONFIG_SAVE_LED is enabled.
 */
esp_err_t led_init(void);

/**
 * Turn the LED on (sets GPIO high) and persist the state to NVS if enabled.
 */
esp_err_t led_on(void);

/**
 * Turn the LED off (sets GPIO low) and persist the state to NVS if enabled.
 */
esp_err_t led_off(void);

/**
 * Toggle the LED state and persist it to NVS if enabled.
 */
esp_err_t led_toggle(void);

/**
 * Get the current LED state.
 *
 * @param state output: 0 (off) or 1 (on)
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t get_led_state(int *state);

/**
 * Release the mutex used by the simple LED. Safe to call once led_init()
 * has been called; does nothing if never initialized.
 */
esp_err_t close_simple_led(void);

#endif // SIMPLE_LED_H_
