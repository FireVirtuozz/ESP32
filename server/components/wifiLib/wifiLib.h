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
#include <esp_mac.h>
#include "mqttLib.h"

//Initialize wifi station, waiting to get IP from router
void wifi_init_sta(void);

//Initialize ESP wifi on AP
void wifi_init_ap(void);

//Initialize ESP wifi on APSTA
void wifi_init_apsta(void);

//Get the IP address of ESP
const char* wifi_get_ip(void);

//get info of APs around
void wifi_scan_aps();

//get wifi info of esp
void wifi_scan_esp();

//get current AP info
void get_ap_info();

#endif