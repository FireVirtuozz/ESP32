#ifndef LEDLIB_H_
#define LEDLIB_H_

#include "driver/gpio.h"
#include <nvs_flash.h>
#include <esp_err.h>
#include <esp_log.h>
#include <nvs.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define LED_PIN 2  //LED pin connected on breadboard

//Initialize led pin
void led_init(void);

//Activate led
void led_on(void);

//Deactivate led
void led_off(void);

//Toggle led
void led_toggle(void);

void close_led();

int get_led_state();

#endif