#ifndef ADDR_RGB_LED_H_
#define ADDR_RGB_LED_H_

#include <inttypes.h>
#include <esp_err.h>

// WS2812B: addressable RGB LED, commonly used as ESP dev-board built-in LEDs.
// Uses the RMT peripheral, not LEDC (no PWM timer/channel involved here).

/**
 * Configure the RMT TX channel and encoder used to drive the WS2812B.
 */
esp_err_t init_ws2812_rmt(void);

/**
 * Set the WS2812B color.
 *
 * @param r red component (0-255)
 * @param g green component (0-255)
 * @param b blue component (0-255)
 */
esp_err_t set_led_color(uint8_t r, uint8_t g, uint8_t b);

#endif // ADDR_RGB_LED_H_
