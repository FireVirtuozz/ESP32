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

// Initialise le WiFi en mode station et attend connexion
void wifi_init_sta(void);

// Récupère l'IP actuelle de l'ESP32
const char* wifi_get_ip(void);

#endif