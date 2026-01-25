#include <stdio.h>
#include "wifiLib.h"
#include "wsLib.h"
#include "udpLib.h"
#include "ledLib.h"
#include "nvsLib.h"
#include "efuseLib.h"
#include "otaLib.h"
#include "mqttLib.h"
#include "screenLib.h"
#include <stdarg.h>

//static const char * TAG = "main";

#define IN_AP_MODE 0

void app_main()
{
    //init_queue_mqtt();

    //ESP_LOGI(TAG, "[APP] Startup..");
    //ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    //ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);

    nvs_init(); //init memory first, wifi/led needs this..

    #if IN_AP_MODE
        wifi_init_ap();
        ws_server_init();
    #else
        wifi_init_sta();
        udp_server_init();
        //mqtt_app_start();
        //ws_server_init();
    #endif

    //wifi_init_apsta(); useful only if esp32 = router
    
    led_init(); //init led pin 2

    ssd1306_setup(); //init oled screen

    init_ledc(); //init pwm pin 4

    screen_full_off();

    ssd1306_draw_string(wifi_get_ip(), 0, 0);

    //print_esp_info_ledc();

}