#ifndef WSLIB_H_
#define WSLIB_H_

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "esp_netif.h"
#include "esp_eth.h"

// Handle HTTP server global
extern httpd_handle_t server;

// Initialise le serveur WebSocket
void ws_server_init(void);

// Envoie un message texte à tous les clients connectés
esp_err_t ws_send_text(const char *msg);

#endif