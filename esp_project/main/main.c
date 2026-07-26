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

#if CONFIG_USE_CAMERA
#include "cameraLib.h"
#endif

#if CONFIG_USE_ZIGBEE
#include "zigbeeLib.h"
#endif

#include "systemLib.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "otaLib.h"

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
    vTaskDelay(pdMS_TO_TICKS(8000));
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
    
    //vTaskDelay(pdMS_TO_TICKS(10000));
    //print_chip_info();

#if CONFIG_DEBUG_WIFI
    wifi_scan_esp();
    wifi_scan_aps();
#endif

#if CONFIG_USE_CAMERA
    vTaskDelay(pdMS_TO_TICKS(10000));
    camera_init();
#endif
    log_msg(TAG, "yo");
/*
    int16_t percent = 0;
    while(1) {
        percent = 0;
        ledc_ky029(0, true);
        ledc_ky029(0, false);
        while (percent <= 100) {
            ledc_ky029(percent, true);
            ledc_ky029(percent / 4, false);
            percent++;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        percent = 0;
        ledc_ky029(0, true);
        while (percent <= 100) {
            ledc_ky029(percent, false);
            percent++;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        percent = 0;
        ledc_ky029(0, false);
        while (percent <= 100) {
            ledc_ky029(percent, true);
            percent++;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
        */

        /*
    int16_t percent = 0;
    while(1) {
        percent = 0;
        ledc_ky009(0, 0);
        ledc_ky009(0, 1);
        ledc_ky009(0, 2);
        while (percent <= 100) {
            ledc_ky009(percent, 0);
            ledc_ky009(percent, 1);
            ledc_ky009(percent, 2);
            percent++;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        for (int i = 0; i < 3; i++) {
            percent = 0;
            ledc_ky009(percent, i);
            ledc_ky009(percent, (i + 1) % 3);
            while (percent <= 100) {
                ledc_ky009(percent, (i + 2) % 3);
                percent++;
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
    }
        */
       
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