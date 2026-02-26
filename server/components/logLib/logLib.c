#include "logLib.h"

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

#define LOG_BUFFER_SIZE 256

//#include "esp_log_write.h" for esp_log_set_vprintf, override ESP_LOGx function. Performance loss.

static const char* TAG = "log_library";

#if LOG_UDP || LOG_MQTT
static QueueHandle_t log_queue;
#endif

static void log_msg_va(const log_level_t level, const char* tag, const char* fmt, va_list args) {
    if (level < LOG_LEVEL) return;

    char buf[LOG_BUFFER_SIZE];
    vsnprintf(buf, sizeof(buf), fmt, args);

    #if LOG_SERIAL
    switch (level) {
        case ESP_LOG_INFO:    ESP_LOGI(tag, "%s", buf); break;
        case ESP_LOG_WARN:    ESP_LOGW(tag, "%s", buf); break;
        case ESP_LOG_ERROR:   ESP_LOGE(tag, "%s", buf); break;
        case ESP_LOG_DEBUG:   ESP_LOGD(tag, "%s", buf); break;
        case ESP_LOG_VERBOSE: ESP_LOGV(tag, "%s", buf); break;
        case ESP_LOG_NONE: break;
        default: break;
    }
    #endif

#if LOG_UDP || LOG_MQTT
    xQueueSend(log_queue, buf, (TickType_t)0);
#endif
}

void log_msg(const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_msg_va(esp_log_level_get(tag), tag, fmt, args);
    va_end(args);
}

void log_msg_lvl(const log_level_t level, const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_msg_va(level, tag, fmt, args);
    va_end(args);
}

#if LOG_UDP || LOG_MQTT
static void log_task(void *arg) {
    char entry[LOG_BUFFER_SIZE];
    while(1) {
        xQueueReceive(log_queue, entry, portMAX_DELAY);
        #if LOG_UDP
        //udp_send(entry.buf);
        #endif
        #if LOG_MQTT
        //mqtt_publish(entry.buf);
        #endif
    }
}
#endif

esp_err_t log_init() {

    //for internal logs
    esp_log_level_set("*", LOG_LEVEL);

    //for our own libraries
    esp_log_level_set("udp_library", ESP_LOG_WARN);
    esp_log_level_set("mqtt_library", ESP_LOG_WARN);
    esp_log_level_set("nvs_library", ESP_LOG_WARN);
    esp_log_level_set("udp_library", ESP_LOG_WARN);
    esp_log_level_set("wifi_library", ESP_LOG_WARN);
    esp_log_level_set("ota_library", ESP_LOG_WARN);
    esp_log_level_set("led_library", ESP_LOG_WARN);
    esp_log_level_set("cmd_library", ESP_LOG_WARN);
    esp_log_level_set("lcd_library", ESP_LOG_WARN);
    esp_log_level_set("ws_library", ESP_LOG_WARN);
    esp_log_level_set("log_library", ESP_LOG_WARN);
    esp_log_level_set("screen_library", ESP_LOG_WARN);

    #if LOG_UDP || LOG_MQTT
    log_queue = xQueueCreate(64, LOG_BUFFER_SIZE);
    xTaskCreatePinnedToCore(log_task, "log_task", 4096, NULL, 2, NULL, 0);
    #endif

    return ESP_OK;
}

dump_t * dump_init(const char* tag) {

    dump_t * dump = (dump_t*)malloc(sizeof(struct dump_st));
    if (dump == NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error allocating dump");
        return NULL;
    }


    int written = snprintf(dump->buffer, sizeof(dump->buffer), "[%s]\n", tag);
    if (written > 0) {
        dump->offset = written;
    } else {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error initializing dump");
        free(dump);
        return NULL;
    }

    return dump;
}

esp_err_t dump_add_line(dump_t * dump, const char* fmt, ...) {
    if (dump == NULL) return ESP_ERR_INVALID_ARG;

    va_list args;
    va_start(args, fmt);

    char *addr_str = &dump->buffer[dump->offset];
    int written = vsnprintf(addr_str, BUFFER_DUMP_SIZE - dump->offset, fmt, args);

    va_end(args);

    if (written > 0) {
        dump->offset += written;
        if (dump->offset < BUFFER_DUMP_SIZE - 1) {
            dump->buffer[dump->offset] = '\n';
            dump->offset++;
        }
    } else {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error writing line to dump");
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

esp_err_t dump_deploy(dump_t ** dump) {
    if (*dump == NULL) return ESP_ERR_INVALID_ARG;

    #if LOG_UDP
    //udp_send(entry.buf);
    //maybe a special frame according to max bytes / message
    #endif
    #if LOG_MQTT
    //mqtt_publish(entry.buf);
    //maybe a special frame according to max bytes / message
    #endif

    free(*dump);
    *dump=NULL; //nullify for user

    return ESP_OK;
}