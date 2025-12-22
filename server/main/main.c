#include <stdio.h>
#include "wifiLib.h"
#include "wsLib.h"
#include "ledLib.h"

void app_main(void)
{
    wifi_init_sta();
    printf("IP: %s\n", wifi_get_ip());
    ws_server_init();
    led_init();
}