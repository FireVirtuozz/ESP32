#include <stdio.h>
#include "wifiLib.h"
#include "wsLib.h"
#include "ledLib.h"
#include "nvsLib.h"

void app_main(void)
{
    nvs_init(); //init memory first, wifi/led needs this..

    wifi_init_sta(); //init wifi
    printf("IP: %s\n", wifi_get_ip());

    ws_server_init(); //init websocket

    led_init(); //init led

    list_storage(); //list used keys of type i32 in nvs
}