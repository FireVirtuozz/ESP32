#ifndef LCD_LVGL_LIB_H_
#define LCD_LVGL_LIB_H_

#include <inttypes.h>

void lcd_init();

void set_bar_steer(const int32_t v);

void set_bar_motor(const int32_t v);

void set_label_ip(const char* ip_str);

#endif