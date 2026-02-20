#ifndef MQTTLIB_H_
#define MQTTLIB_H_

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "esp_event.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include <esp_crt_bundle.h>
#include <stdarg.h>
#include <stdbool.h>

#include "ledLib.h"
#include "nvsLib.h"
#include "efuseLib.h"
#include "otaLib.h"
#include "wifiLib.h"
#include "screenLib.h"
#include "cJSON.h"

typedef enum {
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3,
    LOG_DEBUG = 0
} mqtt_log_level;

//init queue for debug messages
void init_queue_mqtt();

//init mqtt
void mqtt_start();

//new log
void log_mqtt(mqtt_log_level lvl, const char * tag, bool mqtt, const char* fmt, ...);

#endif