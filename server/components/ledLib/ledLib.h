#ifndef LEDLIB_H_
#define LEDLIB_H_

#include "driver/gpio.h"
#include <esp_err.h>
#include <esp_log.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvsLib.h"
#include "mqttLib.h"
#include "driver/ledc.h"
#include "soc/soc_caps.h"

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

//Print info on pwm signals ledc
void print_esp_info_ledc();

//init ledc for pwm signals
void init_ledc();

//apply duty with fade
void ledc_duty_fade(const uint32_t duty);

//apply duty directly
void ledc_duty(const uint32_t duty);

//apply angle to servo
void ledc_angle(int16_t angle);

//get current servo angle
uint8_t get_servo_angle();

#endif