#include <stdio.h>

#if CONFIG_USE_WIFI
#include "wifiLib.h"
#endif

#if CONFIG_USE_WSLIB
#include "wsLib.h"
#endif

#if CONFIG_USE_UDPLIB
#include "udpLib.h"
#endif

#if CONFIG_USE_LEDLIB
#include "ledLib.h"
#endif

#include "nvsLib.h"

#if CONFIG_USE_MQTTLIB
#include "mqttLib.h"
#endif

#include "logLib.h"
#include <stdarg.h>

#if CONFIG_USE_LVGL_SCREEN
#include "lcdLib.h"
#endif

#if CONFIG_USE_SCREENLIB
#include "screenLib.h"
#endif

#if CONFIG_USE_SENSORS
#include "sensorsLib.h"
#endif

#if CONFIG_USE_ESPNOW
#include "espnowLib.h"
#endif

#include "cameraLib.h"

#include "systemLib.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char * TAG = "main";

void app_main()
{
    log_init();
    
#if DEBUG_GPIO
    dump_gpio_stats();
#endif
    //init_queue_mqtt();

    //init memory first, wifi/led needs this..
    nvs_init(); 
#if CONFIG_USE_LVGL_SCREEN
    lcd_init();
#endif

#if CONFIG_USE_SCREENLIB
    ssd1306_setup(); //init oled screen
    screen_full_off();
#endif

#if CONFIG_USE_WIFI
    wifi_init();
#endif

#if CONFIG_USE_UDPLIB
    udp_server_init();
#endif
#if CONFIG_USE_MQTTLIB
    mqtt_start();
#endif
#if CONFIG_USE_WSLIB
    ws_server_init();
#endif

#if CONFIG_USE_LEDLIB

    init_all_gpios();
    led_on();
#endif

#if CONFIG_USE_LVGL_SCREEN
    set_label_ip(wifi_get_ip());
#endif
#if CONFIG_USE_SCREENLIB
    ssd1306_draw_string(wifi_get_ip(), 0, 0);
#endif

#if CONFIG_USE_SENSORS
    start_monitoring_task();
#endif

#if CONFIG_USE_UDPLIB
    udp_client_init();
#endif

#if CONFIG_USE_ESPNOW
    espnow_init();
#endif

    

    //vTaskDelay(pdMS_TO_TICKS(20000));
    print_chip_info();

#if CONFIG_DEBUG_WIFI
    wifi_scan_esp();
    wifi_scan_aps();
#endif

    vTaskDelay(pdMS_TO_TICKS(20000));
    camera_init();
    
}