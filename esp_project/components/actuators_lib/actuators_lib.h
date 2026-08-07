#ifndef ACTUATORS_LIB_H_
#define ACTUATORS_LIB_H_

#include <inttypes.h>
#include <esp_err.h>
#include "driver/ledc.h"

#include "h_bridge.h"
#include "servo.h"
#include "simple_led.h"
#include "two_color_led.h"
#include "buzzer.h"
#include "rgb_led.h"
#include "addr_rgb_led.h"
#include "debug_helper.h"

// Shared helper macro: max duty value for a given LEDC resolution (2^res - 1)
#define GET_MAX_DUTY(res) ((1 << (res)) - 1)

/**
 * Initialize every actuator enabled via Kconfig (CONFIG_USE_xxx).
 * Each actuator owns its full LEDC timer + channel configuration internally;
 * this function only orchestrates the calls in a safe order.
 *
 * @return ESP_OK on success, or the first error encountered (init stops on first failure).
 */
esp_err_t init_all_actuators(void);

/**
 * Stop and reset every actuator that was initialized via init_all_actuators().
 * Safe to call even if some actuators were never initialized (each close_xxx()
 * guards its own state).
 *
 * @return ESP_OK on success, ESP_FAIL if at least one actuator failed to close cleanly
 *         (all actuators are still attempted regardless of individual failures).
 */
esp_err_t close_all_actuators(void);

/**
 * @deprecated Kept for backward compatibility with existing call sites.
 * Use init_all_actuators() instead.
 */
esp_err_t init_all_gpios(void);

/**
 * @deprecated Kept for backward compatibility with existing call sites.
 * Use close_all_actuators() instead. Unlike the old implementation, this
 * now also closes the H-Bridge (BTS7960) actuator.
 */
esp_err_t close_led(void);

/**
 * Apply a duty cycle to a given LEDC channel and push the update to hardware.
 * Shared helper used by every actuator; replaces the previous indexed
 * ledc_channel[] lookup table (removed: each actuator already knows its own
 * speed_mode + channel constants, so the extra indirection added no value).
 *
 * @param speed_mode LEDC_LOW_SPEED_MODE or LEDC_HIGH_SPEED_MODE
 * @param channel    the LEDC channel to update
 * @param duty       raw duty value (0..2^resolution-1)
 * @return ESP_OK on success, or the underlying ledc driver error
 */
esp_err_t ledc_apply_duty(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t duty);

#endif // ACTUATORS_LIB_H_
