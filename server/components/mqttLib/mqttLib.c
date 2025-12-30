#include "mqttLib.h"

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

//mqtt through websocket secure

//command to add dependency
//idf.py add-dependency espressif/mqtt

#define BROKER_URI "wss://1fa8d24b9815409da211d026bc02b50f.s1.eu.hivemq.cloud:8884/mqtt"
#define BROKER_USER "ESP32"
#define BROKER_PASS "BigEspGigaChad32"

#define QUEUE_SIZE 256

static const char *TAG = "mqtt_library";

static esp_mqtt_client_handle_t client = NULL;

static QueueHandle_t xQueue;

static bool mqtt_connected = false;

typedef struct {
    char topic[64];
    char payload[256];
    int qos;
    bool retain;
} mqtt_msg_t;

static void handle_commands(char * data) {
    //compare messages to give instructions
    if (strcmp(data, "LED_ON") == 0) {
        log_mqtt(LOG_INFO, TAG, true, "Start Led On");
        led_on();
    } else if (strcmp(data, "LED_OFF") == 0) {
        log_mqtt(LOG_INFO, TAG, true, "Start Led Off");
        led_off();
    } else if (strcmp(data, "LED_TOGGLE") == 0) {
        log_mqtt(LOG_INFO, TAG, true, "Toggling LED");
        led_toggle();
    } else if (strcmp(data, "WIFI_SCAN") == 0) {
        log_mqtt(LOG_INFO, TAG, true, "Starting Wifi Scan");
        wifi_scan_aps();
    } else if (strcmp(data, "ESP_WIFI_INFO") == 0) {
        log_mqtt(LOG_INFO, TAG, true, "Starting ESP Scan");
        wifi_scan_esp();
    } else if (strcmp(data, "CHIP_INFO") == 0) {
        log_mqtt(LOG_INFO, TAG, true, "Printing chip info");
        print_chip_info();
    } else if (strcmp(data, "LIST_NVS_STORAGE") == 0) {
        log_mqtt(LOG_INFO, TAG, true, "Listing NVS storage");
        list_storage();
    } else if (strcmp(data, "OTA_UPDATE") == 0) {
        log_mqtt(LOG_INFO, TAG, true, "Starting OTA update");
        ota_init();
    } else {
        log_mqtt(LOG_INFO, TAG, true, "Event unkown");
    }
    
}

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    client = event->client;
    int msg_id;
    char topic[event->topic_len + 1];
    char data[event->data_len + 1];
    // your_context_t *context = event->context;
    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        //ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        log_mqtt(LOG_INFO, TAG, false, "MQTT_EVENT_CONNECTED");

        msg_id = esp_mqtt_client_subscribe(client, "/commands/#", 0);
        //ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
        //esp_mqtt_client_publish(client, "/commands/qos1", "commence", 0, 1, 0);
        log_mqtt(LOG_INFO, TAG, true, "Sent subscribe to /commands/# successful, msg_id=%d", msg_id);

        break;
    case MQTT_EVENT_DISCONNECTED:
        //ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        log_mqtt(LOG_INFO, TAG, false, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        //ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d, return code=0x%02x ", event->msg_id, (uint8_t)*event->data);
        log_mqtt(LOG_INFO, TAG, false,
            "MQTT_EVENT_SUBSCRIBED, msg_id=%d, return code=0x%02x ", event->msg_id, (uint8_t)*event->data);
        esp_mqtt_client_publish(client, "/logs/start", "starting ESP", 0, 0, 0);
        mqtt_connected = true;
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        //ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        log_mqtt(LOG_INFO, TAG, false,
            "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        //ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        log_mqtt(LOG_INFO, TAG, false,
            "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        //ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        //log_mqtt(LOG_INFO, TAG, false, "MQTT_EVENT_DATA");
        
        memcpy(topic, event->topic, event->topic_len);
        topic[event->topic_len] = '\0';
        if (strcmp(topic, "/commands") == 0) {
            
            memcpy(data, event->data, event->data_len);
            data[event->data_len] = '\0';
            log_mqtt(LOG_INFO, TAG, true,
                "commands event : %s", data);
            handle_commands(data);
        }
        break;
    case MQTT_EVENT_ERROR:
        //ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        log_mqtt(LOG_INFO, TAG, false, "MQTT_EVENT_ERROR");
        break;
    default:
        //ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        log_mqtt(LOG_INFO, TAG, false,
            "Other event id:%d", event->event_id);
        break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    /* The argument passed to esp_mqtt_client_register_event can de accessed as handler_args*/
    //ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    log_mqtt(LOG_DEBUG, TAG, false,
            "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    mqtt_event_handler_cb(event_data);
}

static void log_task(void *pvParameters) {
    mqtt_msg_t m;

    while (1) {
        if (mqtt_connected) {
            if (xQueueReceive(xQueue, &m, portMAX_DELAY) == pdTRUE) {
                {
                    //log_mqtt(LOG_INFO, TAG, false, "Trying MQQT publish: %s", m.payload);
                    if (esp_mqtt_client_publish(client, m.topic, m.payload, strlen(m.payload), m.qos, m.retain) < 0) {
                        log_mqtt(LOG_WARN, TAG, false, "MQTT publish failed: %s", m.payload);
                    }
                }
            }
        }
        vTaskDelay((TickType_t)5);
    }
}

void log_mqtt(mqtt_log_level lvl, const char * tag, bool mqtt, const char* fmt, ...) {
    char buf[256];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // Print on UART
    switch (lvl) {
        case LOG_INFO:  ESP_LOGI(tag, "%s", buf); break;
        case LOG_WARN:  ESP_LOGW(tag, "%s", buf); break;
        case LOG_ERROR: ESP_LOGE(tag, "%s", buf); break;
        case LOG_DEBUG: ESP_LOGD(tag, "%s", buf); break;
    }

    // Send MQTT if activated
    if (mqtt) {
        mqtt_msg_t m;
        m.qos = 0;
        m.retain = false;
        snprintf(m.topic, sizeof(m.topic), "/logs/%s", tag);
        snprintf(m.payload, sizeof(m.payload), "[%s] %.200s", tag, buf);

        if (xQueueSend(xQueue, &m, 0) != pdTRUE) {
            log_mqtt(LOG_ERROR, TAG, false, "Fail to send message to Queue");
        }
    }
}

void mqtt_app_start()
{
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = BROKER_URI,
        .credentials.username = BROKER_USER,
        .credentials.authentication.password = BROKER_PASS,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .network.disable_auto_reconnect = false,
    };

    //ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    log_mqtt(LOG_DEBUG, TAG, false,
            "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    esp_mqtt_client_start(client);
}

void init_queue_mqtt() {

    xQueue = xQueueCreate(QUEUE_SIZE, sizeof(mqtt_msg_t));
    if (xQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create MQTT queue");
        return;
    }

    BaseType_t result = xTaskCreate(
        log_task,          // function pointer
        "mqtt_log_task",   // task name
        4096,              // stack size
        NULL,              // task parameter
        5,                 // priority
        NULL               // task handle
    );

    if (result != pdPASS) {
        log_mqtt(LOG_ERROR, TAG, false, "Failed to create log task");
    }
}


