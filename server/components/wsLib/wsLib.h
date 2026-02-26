#ifndef WSLIB_H_
#define WSLIB_H_

#include <esp_err.h>

// Initialize websocket server
void ws_server_init();

// Send a message to all clients
esp_err_t ws_send_text(const char *msg);

#endif