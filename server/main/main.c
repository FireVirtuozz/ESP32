#include <stdio.h>
#include "wifiLib.h"
#include "wsLib.h"
#include "ledLib.h"
#include "nvsLib.h"
#include "efuseLib.h"
#include "otaLib.h"

void app_main(void)
{
    nvs_init(); //init memory first, wifi/led needs this..

    /*
    wifi_init_sta(); //init wifi
    printf("IP: %s\n", wifi_get_ip());

    ws_server_init(); //init websocket
    */

    /*
    wifi_init_ap();
    */

    wifi_init_apsta();
    
    led_init(); //init led

    ota_init();

    list_storage(); //list used keys of type i32 in nvs

    wifi_scan_aps(); //scan APs

    wifi_scan_esp();

    print_chip_info();
}