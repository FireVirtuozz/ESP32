#ifndef WIFILIB_H_
#define WIFILIB_H_

#include <esp_err.h>

//Get the IP address of ESP
const char* wifi_get_ip(void);

//Initialize wifi according to config (apsta, sta, ap)
void wifi_init();

#if CONFIG_DEBUG_WIFI
//get info of APs around
void wifi_scan_aps();

//get wifi info of esp
void wifi_scan_esp();

//get current AP info
void get_ap_info();
#endif

//check if there already is a wifi scan
bool is_scanning();

esp_err_t sta_get_rssi(int *rssi);

#endif