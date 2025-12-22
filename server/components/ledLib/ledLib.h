#ifndef LEDLIB_H_
#define LEDLIB_H_

#include "driver/gpio.h"

#define LED_PIN 2  // par exemple la LED onboard

void led_init(void);
void led_on(void);
void led_off(void);
void led_toggle(void);

#endif