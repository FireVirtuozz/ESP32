# Screen LCD Library

Screen library for SSD1306 Oled Screen, using ESP's LCD Component & LVGL.

https://docs.lvgl.io/master/introduction/index.html

## Setup

```bash
idf.py add-dependency lvgl/lvgl
idf.py fullclean build
```

The ESP LCD works with:
- Master I2C bus : I2C bus of ESP
- Panel I2C IO : I2C device communication to the screen
- Panel Driver: buffer operations of screen

LVGL is here to add animations, complex components on screen like scroll, color.. 

ESP LCD is here to apply the LVGL's buffer to the physical screen.

## How it works

The workflow is:

1) Add components to a LVGL display (e.g., Label)
    * Only modifies the LVGL internal buffer

2) On each LVGL tick (controlled by ESP Timer), LVGL calculates what changed
    
3) LVGL schedules a flush, calling your flush_cb
    * Copy LVGL’s buffer → oled_buffer (conversion from LVGL horizontal bytes buffer to SSD1306 vertical bytes buffer)
    * Call esp_lcd_panel_draw_bitmap() to update the hardware

4) Once the ESP LCD driver finishes the flush, it calls flush_ready callback
    * LVGL is notified that it can schedule new flushes or continue animations