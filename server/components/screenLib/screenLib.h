#ifndef SCREENLIB_H_
#define SCREENLIB_H_

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_system.h"
#include "esp_log.h"
#include "mqttLib.h"

void ssd1306_setup();

void screen_full_on();

void screen_full_off();

void ssd1306_draw_string(const char *str, int x, int page);

#endif