#ifndef TWO_COLOR_LED_H_
#define TWO_COLOR_LED_H_

#include <inttypes.h>
#include <esp_err.h>
#include "driver/ledc.h"

// KY-029 / KY-011: two-color LED (red/green), PWM-driven, shared cathode

#if !CONFIG_IDF_TARGET_ESP32C6

#define KY029_RED_GPIO          18  // adjust to your wiring
#define KY029_GREEN_GPIO        19  // adjust to your wiring
#define KY029_RESOLUTION        LEDC_TIMER_13_BIT
#define KY029_FREQ              5000 // 5kHz, flicker-free for LEDs
#define KY029_MIN_DUTY          0
#define KY029_MAX_DUTY          GET_MAX_DUTY(KY029_RESOLUTION) // full brightness, no /2
#define KY029_SPEED_MODE        LEDC_LOW_SPEED_MODE
#define KY029_LEDC_TIMER        LEDC_TIMER_3
#define KY029_RED_CHANNEL       LEDC_CHANNEL_5
#define KY029_GREEN_CHANNEL     LEDC_CHANNEL_6

extern ledc_timer_config_t ledc_timer_ky029;

/**
 * Configure the KY029's LEDC timer and both (red/green) channels.
 */
esp_err_t init_two_color_led(void);

/**
 * Pause and reset the KY029's LEDC timer.
 */
esp_err_t close_two_color_led(void);

/**
 * Apply an intensity to one of the two channels, clamped to [0, 100] percent.
 *
 * @param ky029_percent desired intensity
 * @param red            true for the red channel, false for the green channel
 */
esp_err_t ledc_ky029(int16_t ky029_percent, bool red);

#endif

#endif // TWO_COLOR_LED_H_
