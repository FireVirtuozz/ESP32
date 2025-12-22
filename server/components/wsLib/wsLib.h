#ifndef WSLIB_H_
#define WSLIB_H_

#include <esp_http_server.h>
#include <esp_err.h>
#include "ledLib.h"

// Initialise le serveur WebSocket
void ws_server_init(void);

// Envoie un message texte à tous les clients (simplifié pour 1 client)
esp_err_t ws_send_text(const char *msg);

#endif