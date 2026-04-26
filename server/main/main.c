#define USE_LEDLIB 1
#define USE_SCREENLIB 0
#define USE_LVGL_SCREEN 0
#define USE_UDPLIB 1
#define USE_WSLIB 0
#define USE_MQTTLIB 0
#define USE_SENSORS 1

#include <stdio.h>
#include "wifiLib.h"

#if USE_WSLIB
#include "wsLib.h"
#endif

#if USE_UDPLIB
#include "udpLib.h"
#endif

#if USE_LEDLIB
#include "ledLib.h"
#endif

#include "nvsLib.h"

#if USE_MQTTLIB
#include "mqttLib.h"
#endif

#include "logLib.h"
#include <stdarg.h>

#if USE_LVGL_SCREEN
#include "lcdLib.h"
#endif

#if USE_SCREENLIB
#include "screenLib.h"
#endif

#if USE_SENSORS
#include "sensorsLib.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#include "systemLib.h"

static const char * TAG = "main";

void app_main()
{
    log_init();
    
#if DEBUG_GPIO
    dump_gpio_stats();
#endif
    //init_queue_mqtt();

    nvs_init(); //init memory first, wifi/led needs this..

#if USE_LVGL_SCREEN
    lcd_init();
#endif
#if USE_SCREENLIB
    ssd1306_setup(); //init oled screen
    screen_full_off();
#endif

    wifi_init();
#if USE_UDPLIB
    //udp_server_init();
#endif
#if USE_MQTTLIB
    mqtt_start();
#endif
#if USE_WSLIB
    ws_server_init();
#endif

#if USE_LEDLIB
/*
    init_all_gpios();
    led_on();*/
#endif

#if USE_LVGL_SCREEN
    set_label_ip(wifi_get_ip());
#endif
#if USE_SCREENLIB
    ssd1306_draw_string(wifi_get_ip(), 0, 0);
#endif

#if USE_SENSORS

    start_monitoring_task();

#endif

    udp_client_init();

    //print_chip_info();
    
}