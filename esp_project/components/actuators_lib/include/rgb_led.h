#ifndef RGB_LED_H_
#define RGB_LED_H_

#include <inttypes.h>
#include <esp_err.h>
#include "driver/ledc.h"

// KY-009 / KY-016: RGB LED (no built-in resistors, add your own), PWM-driven

#if !CONFIG_IDF_TARGET_ESP32C6 //if c6, not enough ledc timers (<= 6)

//TODO: Take Automatic LEDC Channel / Timer according to KConfig

#define KY009_RED_GPIO          18  // adjust to your wiring
#define KY009_GREEN_GPIO        19  // adjust to your wiring
#define KY009_BLUE_GPIO         17  // adjust to your wiring
#define KY009_RESOLUTION        LEDC_TIMER_13_BIT
#define KY009_FREQ              5000 // 5kHz, flicker-free for LEDs
#define KY009_MIN_DUTY          0
#define KY009_MAX_DUTY          GET_MAX_DUTY(KY009_RESOLUTION) // full brightness, no /2
#define KY009_SPEED_MODE        LEDC_LOW_SPEED_MODE
#define KY009_LEDC_TIMER        LEDC_TIMER_3 // different frequency than the buzzer, own timer

#define KY009_RED_CHANNEL       LEDC_CHANNEL_5
#define KY009_GREEN_CHANNEL     LEDC_CHANNEL_6
#define KY009_BLUE_CHANNEL      LEDC_CHANNEL_7

extern ledc_timer_config_t ledc_timer_ky009;

/**
 * Configure the KY009's LEDC timer and its three (red/green/blue) channels.
 */
esp_err_t init_rgb_led(void);

/**
 * Pause and reset the KY009's LEDC timer.
 */
esp_err_t close_rgb_led(void);

/**
 * Apply an intensity to one color channel, clamped to [0, 100] percent.
 *
 * @param ky009_percent desired intensity
 * @param color          0 = red, 1 = green, 2 = blue
 */
esp_err_t ledc_ky009(int16_t ky009_percent, uint8_t color);

#endif

#endif // RGB_LED_H_
