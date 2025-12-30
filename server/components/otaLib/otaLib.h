#ifndef OTALIB_H_
#define OTALIB_H_

#include <esp_log.h>
#include <esp_https_ota.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_check.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include <esp_crt_bundle.h>
#include "mqttLib.h"

void ota_init();

#endif