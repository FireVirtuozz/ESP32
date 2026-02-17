#ifndef LCDLIB_H_
#define LCDLIB_H_

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_system.h"
#include "esp_log.h"
#include "mqttLib.h"
#include "nvsLib.h"

void lcd_init();

void set_bar_steer(int32_t v);

void set_bar_motor(int32_t v);

#endif