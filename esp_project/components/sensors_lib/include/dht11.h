#ifndef DHT11_H_
#define DHT11_H_

#include <esp_err.h>

// DHT11: temperature/humidity sensor, single-wire custom protocol.
// Captured via RMT RX for the precise pulse-width timing the protocol needs
// (too tight to reliably bit-bang on plain GPIO).
#define DHT11_GPIO 23

esp_err_t init_dht11(void);

#endif // DHT11_H_
