#ifndef WIFILIB_H_
#define WIFILIB_H_

#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <nvs_flash.h>
#include <freertos/event_groups.h>
#include <string.h>
#include <esp_wifi.h>

//Initialize wifi station, waiting to get IP from router
void wifi_init_sta(void);

//Get the IP address of ESP
const char* wifi_get_ip(void);

#endif