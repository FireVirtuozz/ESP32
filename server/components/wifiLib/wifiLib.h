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

#define WIFI_AP_MODE 1
#define WIFI_STA_MODE 0

#define DEBUG_WIFI 0

//Get the IP address of ESP
const char* wifi_get_ip(void);

//Initialize wifi according to config (apsta, sta, ap)
void wifi_init();

#if DEBUG_WIFI
//get info of APs around
void wifi_scan_aps();

//get wifi info of esp
void wifi_scan_esp();

//get current AP info
void get_ap_info();
#endif

//check if there already is a wifi scan
bool is_scanning();

#endif