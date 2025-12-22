#include "ledLib.h"
#include "driver/gpio.h"

void led_init(void) {
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
}

void led_on(void) {
    gpio_set_level(LED_PIN, 1);
}

void led_off(void) {
    gpio_set_level(LED_PIN, 0);
}

void led_toggle(void) {
    gpio_set_level(LED_PIN, !gpio_get_level(LED_PIN));
}
