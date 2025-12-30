#ifndef LEDLIB_H_
#define LEDLIB_H_

#include "driver/gpio.h"
#include <esp_err.h>
#include <esp_log.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvsLib.h"
#include "mqttLib.h"

#define LED_PIN 2  //LED pin connected on breadboard

//Initialize led pin
void led_init();

//Activate led
void led_on();

//Deactivate led
void led_off();

//Toggle led
void led_toggle();

//Close led
void close_led();

//Get led state
int get_led_state();

#endif