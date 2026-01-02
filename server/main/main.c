#include <stdio.h>
#include "wifiLib.h"
#include "wsLib.h"
#include "ledLib.h"
#include "nvsLib.h"
#include "efuseLib.h"
#include "otaLib.h"
#include "mqttLib.h"
#include "screenLib.h"
#include <stdarg.h>

//static const char * TAG = "main";

void app_main()
{
    init_queue_mqtt();

    //ESP_LOGI(TAG, "[APP] Startup..");
    //ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    //ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    nvs_init(); //init memory first, wifi/led needs this..

    wifi_init_apsta();

    mqtt_app_start();
    
    led_init(); //init led

    ssd1306_setup(); //init oled screen

    //ota_init();

    //list_storage(); //list used keys of type i32 in nvs

    //wifi_scan_aps(); //scan APs

    //wifi_scan_esp();

    //print_chip_info();

    /*
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // keep main alive
    }
        */
    

}