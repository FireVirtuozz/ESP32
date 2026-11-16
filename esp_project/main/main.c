#include <stdio.h>

#if CONFIG_USE_WIFI
#include "wifi_lib.h"
#endif

#if CONFIG_USE_WSLIB
#include "ws_lib.h"
#endif

#if CONFIG_USE_UDPLIB
#include "udp_lib.h"
#endif

#if CONFIG_USE_LEDLIB
#include "actuators_lib.h"
#endif

#include "nvs_lib.h"

#if CONFIG_USE_MQTTLIB
#include "mqttLib.h"
#endif

#include "log_lib.h"
#include <stdarg.h>

#if CONFIG_USE_LVGL_SCREEN
#include "lcd_lvgl_lib.h"
#endif

#if CONFIG_USE_SCREENLIB
#include "screen_lib.h"
#endif

#if CONFIG_USE_SENSORS
#include "sensors_lib.h"
#endif

#if CONFIG_USE_ESPNOW
#include "espnow_lib.h"
#endif

#if CONFIG_USE_CAMERA
#include "camera_lib.h"
#endif

#if CONFIG_USE_ZIGBEE
#include "zigbee_lib.h"
#endif

#include "system_lib.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ota_lib.h"

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

#if CONFIG_USE_ZIGBEE
    init_zigbee();
    vTaskDelay(pdMS_TO_TICKS(3000));
#endif

#if CONFIG_USE_WIFI
    wifi_init();
#endif

#if CONFIG_USE_UDPLIB
    udp_server_init();
    udp_client_init();
#endif
#if CONFIG_USE_MQTTLIB
    mqtt_start();
#endif
#if CONFIG_USE_WSLIB
    ws_server_init();
#endif

#if CONFIG_USE_LVGL_SCREEN
    set_label_ip(wifi_get_ip());
#endif
#if CONFIG_USE_SCREENLIB
    ssd1306_draw_string(wifi_get_ip(), 0, 0);
#endif

#if CONFIG_USE_SENSORS
    init_sensors();
#endif

#if CONFIG_USE_LEDLIB
    init_all_gpios();
    led_on();
    set_led_color(0, 255, 0);
#endif

#if CONFIG_USE_ESPNOW
    espnow_init();
#endif
    /*
    vTaskDelay(pdMS_TO_TICKS(1000));
    print_chip_info()*/

#if CONFIG_DEBUG_WIFI
    wifi_scan_esp();
    wifi_scan_aps();
#endif

#if CONFIG_USE_CAMERA
    vTaskDelay(pdMS_TO_TICKS(10000));
    camera_init();
#endif
    log_msg(TAG, "MAIN ENDING");

#if CONFIG_USE_ZIGBEE
    log_msg(TAG, "Sending ON command to 0xdaf3...");
    send_cmd_on_off(0xdaf3, 1, true); // TRUE = ON

    vTaskDelay(pdMS_TO_TICKS(5000));

    log_msg(TAG, "Sending OFF command to 0xdaf3...");
    send_cmd_on_off(0xdaf3, 1, false); // FALSE = OFF

    vTaskDelay(pdMS_TO_TICKS(5000));

    log_msg(TAG, "Sending ON command to 0xdaf3...");
    send_cmd_on_off(0xdaf3, 1, true); // FALSE = OFF
#endif

}