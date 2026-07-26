#ifndef ZIGBEELIB_H_
#define ZIGBEELIB_H_

#include <inttypes.h>
#include <esp_err.h>

#if CONFIG_IDF_TARGET_ESP32C6

void init_zigbee(void);

void send_cmd_on_off(uint16_t short_addr, uint8_t ep, bool on);

#endif

#endif