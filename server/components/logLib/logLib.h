#ifndef LOGLIB_H_
#define LOGLIB_H_

#include <inttypes.h>
#include <esp_err.h>
#include <esp_log.h>
#include "esp_log_level.h"

#define LOG_MQTT 0
#define LOG_UDP 0
#define LOG_SERIAL 1

typedef esp_log_level_t log_level_t;

#define LOG_LEVEL (ESP_LOG_INFO)

#define BUFFER_DUMP_SIZE 2048

typedef struct dump_st {
    char buffer[BUFFER_DUMP_SIZE];
    uint16_t offset;
} dump_t;

//log message with level, number of characters < 256, if more use dump
void log_msg_lvl(const log_level_t level, const char* tag, const char* fmt, ...);

//log message, number of characters < 256, if more use dump
void log_msg(const char* tag, const char* fmt, ...);

esp_err_t log_init();

//init large dump
dump_t * dump_init(const char* tag);

esp_err_t dump_add_line(dump_t * dump, const char* fmt, ...);

esp_err_t dump_deploy(dump_t ** dump);

#endif