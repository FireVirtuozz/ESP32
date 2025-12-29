#include <stdio.h>
#include "wifiLib.h"
#include "wsLib.h"
#include "ledLib.h"
#include "nvsLib.h"
#include "efuseLib.h"

#define FW_VERSION "v1.1"
#define FIRMWARE_URL "https://raw.githubusercontent.com/ton-compte/firmware/main/firmware.bin"

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

    list_storage(); //list used keys of type i32 in nvs

    wifi_scan_aps(); //scan APs

    wifi_scan_esp();

    print_chip_info();
}