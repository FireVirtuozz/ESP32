#ifndef OTALIB_H_
#define OTALIB_H_

#include <esp_log.h>
#include <esp_https_ota.h>
#include <esp_event.h>
#include <esp_crt_bundle.h>

void ota_init(const char* firmware_url);

#endif