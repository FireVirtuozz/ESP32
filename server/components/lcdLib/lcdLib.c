#include "lcdLib.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/lock.h>
#include <sys/param.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_system.h"
#include "esp_log.h"
#include "logLib.h"
#include "nvsLib.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_types.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_vendor.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "freertos/idf_additions.h"

#define I2C_BUS_PORT  0
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ    (400 * 1000)
#define EXAMPLE_PIN_NUM_SDA           22
#define EXAMPLE_PIN_NUM_SCL           21
#define EXAMPLE_PIN_NUM_RST           -1
#define EXAMPLE_I2C_HW_ADDR           0x3C

#define EXAMPLE_LCD_H_RES              128
#define EXAMPLE_LCD_V_RES              64

#define EXAMPLE_LCD_CMD_BITS           8
#define EXAMPLE_LCD_PARAM_BITS         8

#define EXAMPLE_LVGL_TICK_PERIOD_MS    5
#define EXAMPLE_LVGL_TASK_STACK_SIZE   (4 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY     2
#define EXAMPLE_LVGL_PALETTE_SIZE      8
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1000 / CONFIG_FREERTOS_HZ

#define MAX_VALUE 100
#define MIN_VALUE -100

// To use LV_COLOR_FORMAT_I1, we need an extra buffer to hold the converted data
static uint8_t oled_buffer[EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES / 8];
// LVGL library is not thread-safe, this example will call LVGL APIs from different tasks, so use a mutex to protect it
static _lock_t lvgl_api_lock;

lv_obj_t * bar_motor = NULL;
lv_obj_t * bar_steer = NULL;
lv_obj_t * label_ip = NULL;

volatile int32_t ui_bar_steer = 0;
volatile int32_t ui_bar_motor = 0;
int32_t last_ui_bar_steer = 0;
int32_t last_ui_bar_motor = 0;

volatile uint8_t label_ip_text_changed = 0;
volatile char label_ip_text[32] = "Waiting for Wifi startup";

static const char * TAG = "lcd_library";

void set_bar_motor(const int32_t v)
{
    if (bar_motor != NULL){
        ui_bar_motor = v;
    }
}

void set_bar_steer(const int32_t v)
{
    if (bar_steer != NULL){
        ui_bar_steer = v;
    }
}

void set_label_ip(const char* ip_str) {
    if (label_ip != NULL) {
        if (strcmp((char*)label_ip_text, ip_str) != 0) {
            strncpy((char*)label_ip_text, ip_str, sizeof(label_ip_text));
            label_ip_text[sizeof(label_ip_text) - 1] = '\0'; //security if out of bounds
            label_ip_text_changed = 1;
            _lock_acquire(&lvgl_api_lock);
            lv_label_set_text(label_ip, (char*)label_ip_text);
            _lock_release(&lvgl_api_lock);
        }
    }
}

static void event_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target_obj(e);

    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.font = &lv_font_unscii_8;
    //label_dsc.font = LV_FONT_DEFAULT;

    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%d", (int)lv_bar_get_value(obj));

    lv_point_t txt_size;
    lv_text_get_size(&txt_size, buf, label_dsc.font, label_dsc.letter_space, label_dsc.line_space, LV_COORD_MAX,
                     label_dsc.flag);

    lv_area_t txt_area;
    txt_area.x1 = 0;
    txt_area.x2 = txt_size.x - 1;
    txt_area.y1 = 0;
    txt_area.y2 = txt_size.y - 1;

    //indicator text area
    lv_area_t indic_area;
    lv_obj_get_coords(obj, &indic_area);
    lv_area_set_width(&indic_area, lv_area_get_width(&indic_area) *
        (lv_bar_get_value(obj) - MIN_VALUE) / (MAX_VALUE - MIN_VALUE));

    /*If the indicator is long enough put the text inside on the right*/
    if(lv_area_get_width(&indic_area) > txt_size.x + 20) {
        lv_area_align(&indic_area, &txt_area, LV_ALIGN_RIGHT_MID, -3, 0);
        label_dsc.color = lv_color_white(); //black on ssd1306
    }
    /*If the indicator is still short put the text out of it on the right*/
    else {
        lv_area_align(&indic_area, &txt_area, LV_ALIGN_OUT_RIGHT_MID, 3, 0);
        label_dsc.color = lv_color_black(); //white on ssd1306
    }
    
    label_dsc.text = buf;
    label_dsc.text_local = true;

    lv_layer_t * layer = lv_event_get_layer(e);
    lv_draw_label(layer, &label_dsc, &txt_area);
}

/**
 * Example Text Scrolling Circular using LVGL
 * Screen : lv_obj, like "root" in JavaFX, the parent
 * Label : Label type of lv_object, child of Screen (root)
 * 
 * Set Mode if the label is out of bounds, here it is circular scroll
 * Set Text & width & alignment of Label
 */
void example_lvgl_demo_ui(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    label_ip = lv_label_create(scr);
    lv_label_set_long_mode(label_ip, LV_LABEL_LONG_SCROLL_CIRCULAR); /* Circular scroll */
    lv_label_set_text(label_ip, (char*)label_ip_text);
    
    /* Size of the screen (if you use rotation 90 or 270, please use lv_display_get_vertical_resolution) */
    lv_obj_set_width(label_ip, lv_display_get_horizontal_resolution(disp));
    lv_obj_align(label_ip, LV_ALIGN_TOP_MID, 0, 0);

    bar_motor = lv_bar_create(scr);
    lv_bar_set_range(bar_motor, MIN_VALUE, MAX_VALUE);
    lv_obj_set_size(bar_motor, 100, 10);

    /* white background bar (black on ssd1306) */
    lv_obj_set_style_bg_color(bar_motor, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_motor, LV_OPA_COVER, LV_PART_MAIN);

    /* black bar (white on ssd1306)*/
    lv_obj_set_style_bg_color(bar_motor, lv_color_black(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_motor, LV_OPA_COVER, LV_PART_INDICATOR);

    /* black border (white on ssd1306) */
    lv_obj_set_style_border_width(bar_motor, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar_motor, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(bar_motor, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_align_to(bar_motor, label_ip, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    lv_obj_add_event_cb(bar_motor, event_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    set_bar_motor(0);

    bar_steer = lv_bar_create(scr);
    lv_bar_set_range(bar_steer, MIN_VALUE, MAX_VALUE);
    lv_obj_set_size(bar_steer, 100, 8);

    /* white background bar (black on ssd1306) */
    lv_obj_set_style_bg_color(bar_steer, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_steer, LV_OPA_COVER, LV_PART_MAIN);

    /* black bar (white on ssd1306)*/
    lv_obj_set_style_bg_color(bar_steer, lv_color_black(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_steer, LV_OPA_COVER, LV_PART_INDICATOR);

    /* black border (white on ssd1306) */
    lv_obj_set_style_border_width(bar_steer, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar_steer, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(bar_steer, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_align_to(bar_steer, bar_motor, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    lv_obj_add_event_cb(bar_steer, event_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    set_bar_steer(0);
}

/**
 * Notify when ESP LCD finished its operation that LVGL flush is ready
 */
static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t io_panel, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

/**
 * Callback when the flush is done on LVGL
 * 
 * Going through the area & copy the pixel map of LVGL to ESP's LCD Driver
 */
static void example_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);

    // This is necessary because LVGL reserves 2 x 4 bytes in the buffer, as these are assumed to be used as a palette. Skip the palette here
    // More information about the monochrome, please refer to https://docs.lvgl.io/9.2/porting/display.html#monochrome-displays
    px_map += EXAMPLE_LVGL_PALETTE_SIZE;

    uint16_t hor_res = lv_display_get_physical_horizontal_resolution(disp);
    //area : 1 : left top, 2 : right bottom
    int x1 = area->x1;
    int x2 = area->x2;
    int y1 = area->y1;
    int y2 = area->y2;

    //going through the area line by line
    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            //LVGL : bytes displayed horizontally
            /* The order of bits is MSB first
                        MSB           LSB
               bits      7 6 5 4 3 2 1 0
               pixels    0 1 2 3 4 5 6 7
                        Left         Right
            */
            // index (x,y) in px_map (bytesArray) : hor_res/8 (bytes/line) * y + offset x / 8 (current byte)
            // bit position in MSB byte : (7 - x % 8) (x = 11 --> pos = 4)
            // 1 << x : mask to isolate pixel value in byte (x = 4 --> 00010000 = pixel mask )
            bool chroma_color = (px_map[(hor_res >> 3) * y  + (x >> 3)] & 1 << (7 - x % 8));

            /* Write to the buffer as required for the display.
            * It writes only 1-bit for monochrome displays mapped vertically.*/
            //SSD1306 :  bytes displayed vertically at each horizontal page
            //takes the right address of byte
            uint8_t *buf = oled_buffer + hor_res * (y >> 3) + (x);
            //address value updated according to current bit in vertical byte by mask
            if (chroma_color) {
                (*buf) &= ~(1 << (y % 8)); //put to 0 (on)
            } else {
                (*buf) |= (1 << (y % 8)); //put to 1 (off)
            }
        }
    }
    // pass the draw buffer to the driver
    esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2 + 1, y2 + 1, oled_buffer);
}

/**
 * Periodic call for LVGL
 */
static void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

/**
 * Start LVGL task
 * 
 * Call LVGL timer to update LVGL periodically
 */
static void example_lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t time_till_next_ms = 0;
    while (1) {
        _lock_acquire(&lvgl_api_lock);

        if (ui_bar_motor != last_ui_bar_motor) {
            lv_bar_set_value(bar_motor, ui_bar_motor, LV_ANIM_OFF);
            last_ui_bar_motor = ui_bar_motor;
        }
        
        if (ui_bar_steer != last_ui_bar_steer) {
            lv_bar_set_value(bar_steer, ui_bar_steer, LV_ANIM_OFF);
            last_ui_bar_steer = ui_bar_steer;
        }

        /*
        if (label_ip_text_changed) {
            lv_label_set_text(label_ip, (char*)label_ip_text);
            label_ip_text_changed = 0;
        }
            */

        time_till_next_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);
        // in case of triggering a task watch dog time out
        time_till_next_ms = MAX(time_till_next_ms, EXAMPLE_LVGL_TASK_MIN_DELAY_MS);
        // in case of lvgl display not ready yet
        time_till_next_ms = MIN(time_till_next_ms, EXAMPLE_LVGL_TASK_MAX_DELAY_MS);
        usleep(1000 * time_till_next_ms);
    }
}

/**
 * Initialize LCD
 * 
 * Setup master I2C bus, panel IO I2C, panel Driver ESP
 * Reset panel, init & turn off
 * Init LVGL, associate panel driver ESP - display LVGL, create buffer (calloc of bytes in RAM)
 * Set Color format (monochrome), LVGL buffer
 * Register callbacks : on flush LVGL copy buffer on ESP, on transition done on ESP notify LVGL
 * Setup ESP timer periodic task to update LVGL tick
 * Start the LVGL task 
 */
void lcd_init()
{
    ESP_LOGI(TAG, "Initialize I2C bus");
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .i2c_port = I2C_BUS_PORT,
        .sda_io_num = EXAMPLE_PIN_NUM_SDA,
        .scl_io_num = EXAMPLE_PIN_NUM_SCL,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = EXAMPLE_I2C_HW_ADDR,
        .scl_speed_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
        .control_phase_bytes = 1,               // According to SSD1306 datasheet
        .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,   // According to SSD1306 datasheet
        .lcd_param_bits = EXAMPLE_LCD_CMD_BITS, // According to SSD1306 datasheet
        .dc_bit_offset = 6,                     // According to SSD1306 datasheet
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &io_handle));

    ESP_LOGI(TAG, "Install SSD1306 panel driver");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 1,
        .reset_gpio_num = EXAMPLE_PIN_NUM_RST,
    };

    esp_lcd_panel_ssd1306_config_t ssd1306_config = {
        .height = EXAMPLE_LCD_V_RES,
    };
    panel_config.vendor_config = &ssd1306_config;
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "Initialize LVGL");
    lv_init();
    // create a lvgl display
    lv_display_t *display = lv_display_create(EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);
    // associate the i2c panel handle to the display
    lv_display_set_user_data(display, panel_handle);
    // create draw buffer
    void *buf = NULL;
    ESP_LOGI(TAG, "Allocate separate LVGL draw buffers");
    // LVGL reserves 2 x 4 bytes in the buffer, as these are assumed to be used as a palette.
    // size in bits x * y, / 8 --> bytes, + palette (color, even in monochrome)
    size_t draw_buffer_sz = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES / 8 + EXAMPLE_LVGL_PALETTE_SIZE;
    //calloc : init 1 element to 0, in RAM (internal), and of bytes (8 bit)
    buf = heap_caps_calloc(1, draw_buffer_sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(buf);

    // LVGL9 suooprt new monochromatic format.
    lv_display_set_color_format(display, LV_COLOR_FORMAT_I1);
    // initialize LVGL draw buffers
    lv_display_set_buffers(display, buf, NULL, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_FULL);
    // set the callback which can copy the rendered image to an area of the display
    lv_display_set_flush_cb(display, example_lvgl_flush_cb);

    ESP_LOGI(TAG, "Register io panel event callback for LVGL flush ready notification");
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = example_notify_lvgl_flush_ready,
    };
    /* Register done callback */
    esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, display);

    ESP_LOGI(TAG, "Use esp_timer as LVGL tick timer");
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

    ESP_LOGI(TAG, "Create LVGL task");
    xTaskCreatePinnedToCore(example_lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE,
        NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL, 1);
    //xTaskCreate(example_lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "Display LVGL Scroll Text");
    // Lock the mutex due to the LVGL APIs are not thread-safe
    _lock_acquire(&lvgl_api_lock);
    example_lvgl_demo_ui(display);
    _lock_release(&lvgl_api_lock);
}