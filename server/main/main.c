#include <stdio.h>
#include "wifiLib.h"
#include "wsLib.h"
#include "udpLib.h"
#include "ledLib.h"
#include "nvsLib.h"
#include "efuseLib.h"
#include "otaLib.h"
#include "mqttLib.h"
#include <stdarg.h>

#if USE_LVGL_SCREEN
#include "lcdLib.h"
#else
#include "screenLib.h"
#endif

//static const char * TAG = "main";

void app_main()
{
    
#if DEBUG_GPIO
    dump_gpio_stats();
#endif
    //init_queue_mqtt();

#if USE_LVGL_SCREEN
    lcd_init();
#else
    ssd1306_setup(); //init oled screen
    screen_full_off();
#endif

    esp_log_level_set("*", ESP_LOG_INFO);

    nvs_init(); //init memory first, wifi/led needs this..

    wifi_init();
    udp_server_init();
    //mqtt_app_start();
    //ws_server_init();

    //wifi_init_apsta(); useful only if esp32 = router

    init_all_gpios();

#if USE_LVGL_SCREEN
    set_label_ip(wifi_get_ip());
#else
    ssd1306_draw_string(wifi_get_ip(), 0, 0);
#endif

}