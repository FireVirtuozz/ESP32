#ifndef SCREEN_LIB_H_
#define SCREEN_LIB_H_

#include <stdio.h>
#include <inttypes.h>

void ssd1306_setup();

void screen_full_on();

void screen_full_off();

void ssd1306_draw_string(const char *str, int x, int page);

#endif