#ifndef LEDLIB_H_
#define LEDLIB_H_

#include <inttypes.h>

#define DEBUG_GPIO 0
#define DEBUG_LEDC 0

#define USE_LVGL_SCREEN 0
#define WRITE_MOTOR_SCREEN 1
#define WRITE_ANGLE_SCREEN 1

//idx for channels
#define MOTOR_IDX_FWD  0
#define MOTOR_IDX_BWD  1
#define DIRECTION_IDX  2

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

#if DEBUG_LEDC || DEBUG_GPIO
//Print info on pwm signals ledc
void print_esp_info_ledc();
#endif

#if DEBUG_GPIO
//dump gpio on stdout
void dump_gpio_stats();
#endif

//init ledc for pwm signals
void init_ledc();

//apply duty with fade
void ledc_duty_fade(const uint32_t duty, const uint8_t idx);

//apply duty directly
void ledc_duty(const uint32_t duty, const uint8_t idx);

//apply angle to servo
void ledc_angle(int16_t angle);

//apply percent to motor (h-bridge)
void ledc_motor(int16_t motor_percent);

//get current servo angle
uint8_t get_servo_angle();

#endif