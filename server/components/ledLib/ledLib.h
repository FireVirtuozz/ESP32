#ifndef LEDLIB_H_
#define LEDLIB_H_

#include <inttypes.h>
#include <esp_err.h>

//init ledc & led gpio
void init_all_gpios();

//Initialize led gpio basic
void led_init();

//Activate led
void led_on();

//Deactivate led
void led_off();

//Toggle led
void led_toggle();

//Close led gpio & ledc
void close_led();

//Get led state
int get_led_state();

#if CONFIG_DEBUG_LEDC || CONFIG_DEBUG_GPIO
//Print info on pwm signals ledc
void print_esp_info_ledc();
#endif

#if CONFIG_DEBUG_GPIO
//dump gpio on stdout
void dump_gpio_stats();
#endif

//init ledc for pwm signals
void init_ledc();

//apply duty with fade
void ledc_duty_fade(const uint32_t duty, const uint8_t idx);

//apply angle to servo
void ledc_angle(int16_t angle);

//apply percent to motor (h-bridge)
void ledc_motor(int16_t motor_percent);

//get current servo angle
esp_err_t get_servo_angle(uint8_t* angle);

esp_err_t get_motor_percent(int16_t* motor);

esp_err_t set_motor_percent(int16_t motor);

esp_err_t force_motor_stop();

void ledc_buzzer(int16_t buzzer_percent);

void ledc_ky029(int16_t ky029_percent, const bool red);

void ledc_ky009(int16_t ky009_percent, const uint8_t color);

void set_led_color(uint8_t r, uint8_t g, uint8_t b);

esp_err_t apply_config(uint8_t *buf, uint8_t len);

#endif