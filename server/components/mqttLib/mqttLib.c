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
#include "screenLib.h"
#include "cJSON.h"
#include "cmdLib.h"

//mqtt through websocket secure

//command to add dependency
//idf.py add-dependency espressif/mqtt

#define QUEUE_SIZE 256

#define USE_QUEUE_LOGS 0

#define LOG_LEVEL LOG_INFO

static const char *TAG = "mqtt_library";

static esp_mqtt_client_handle_t client = NULL;

static QueueHandle_t xQueue = NULL;

static bool mqtt_connected = false;

static bool initialized = 0;

typedef struct {
    char topic[64];
    char payload[256];
    int qos;
    bool retain;
} mqtt_msg_t;

static void handle_mqtt_data(const char *data, size_t len) {
    char buf[256];
    if (len >= sizeof(buf)) len = sizeof(buf)-1;
    memcpy(buf, data, len);
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        log_mqtt(LOG_ERROR, TAG, true, "JSON parse error");
        return;
    }

    cJSON *cmd = cJSON_GetObjectItem(root, "command");
    if (!cJSON_IsString(cmd)) {
        log_mqtt(LOG_ERROR, TAG, true, "No command string found");
        cJSON_Delete(root);
        return;
    }

    if (strcmp(cmd->valuestring, "LED_ON") == 0) {
        log_mqtt(LOG_INFO, TAG, true, "Start Led On");
        led_on();
    } else if (strcmp(cmd->valuestring, "LED_OFF") == 0) {
        log_mqtt(LOG_INFO, TAG, true, "Start Led Off");
        led_off();
    } else if (strcmp(cmd->valuestring, "LED_TOGGLE") == 0) {
        log_mqtt(LOG_INFO, TAG, true, "Toggling LED");
        led_toggle();
    } else if (strcmp(cmd->valuestring, "SERVO_DUTY") == 0) {
        cJSON *duty = cJSON_GetObjectItem(root, "duty");

        if (cJSON_IsNumber(duty)) {
            log_mqtt(LOG_INFO, TAG, true, "Updating servo duty : %d",
                     duty->valueint);
            ledc_duty(duty->valueint, DIRECTION_IDX);
        } else {
            log_mqtt(LOG_ERROR, TAG, true, "Invalid SERVO_DUTY JSON");
        }

    } else if (strcmp(cmd->valuestring, "MOTOR_DUTY_FWD") == 0) {
        cJSON *duty = cJSON_GetObjectItem(root, "duty");

        if (cJSON_IsNumber(duty)) {
            log_mqtt(LOG_INFO, TAG, true, "Updating motor fwd duty : %d",
                     duty->valueint);
            ledc_duty(duty->valueint, MOTOR_IDX_FWD);
        } else {
            log_mqtt(LOG_ERROR, TAG, true, "Invalid MOTOR_DUTY_FWD JSON");
        }

    } else if (strcmp(cmd->valuestring, "MOTOR_DUTY_BWD") == 0) {
        cJSON *duty = cJSON_GetObjectItem(root, "duty");

        if (cJSON_IsNumber(duty)) {
            log_mqtt(LOG_INFO, TAG, true, "Updating motor bwd duty : %d",
                     duty->valueint);
            ledc_duty(duty->valueint, MOTOR_IDX_BWD);
        } else {
            log_mqtt(LOG_ERROR, TAG, true, "Invalid MOTOR_DUTY_BWD JSON");
        }

    } else if (strcmp(cmd->valuestring, "SET_MOTOR") == 0) {
        cJSON *percent = cJSON_GetObjectItem(root, "percent");

        if (cJSON_IsNumber(percent)) {
            log_mqtt(LOG_INFO, TAG, true, "Updating motor percent : %d",
                     percent->valueint);
            ledc_motor(percent->valueint);
        } else {
            log_mqtt(LOG_ERROR, TAG, true, "Invalid SET_MOTOR JSON");
        }

    } else if (strcmp(cmd->valuestring, "SET_ANGLE") == 0) {
        cJSON *angle = cJSON_GetObjectItem(root, "angle");

        if (cJSON_IsNumber(angle)) {
            log_mqtt(LOG_INFO, TAG, true, "Updating servo angle : %d",
                     angle->valueint);
            ledc_angle(angle->valueint);
        } else {
            log_mqtt(LOG_ERROR, TAG, true, "Invalid SET_ANGLE JSON");
        }

    } else if (strcmp(cmd->valuestring, "WIFI_SCAN") == 0) {
        log_mqtt(LOG_INFO, TAG, true, "Starting Wifi Scan");
#if DEBUG_WIFI
        wifi_scan_aps();
#else
        log_mqtt(LOG_INFO, TAG, true, "Wifi debug not activated");
#endif
    } else if (strcmp(cmd->valuestring, "ESP_WIFI_INFO") == 0) {
        log_mqtt(LOG_INFO, TAG, true, "Starting ESP Scan");
#if DEBUG_WIFI
        wifi_scan_esp();
#else
        log_mqtt(LOG_INFO, TAG, true, "Wifi debug not activated");
#endif
    } else if (strcmp(cmd->valuestring, "CHIP_INFO") == 0) {
        log_mqtt(LOG_INFO, TAG, true, "Printing chip info");
        print_chip_info();
    } else if (strcmp(cmd->valuestring, "LIST_NVS_STORAGE") == 0) {
        log_mqtt(LOG_INFO, TAG, true, "Listing NVS storage");
        list_storage();
    } else if (strcmp(cmd->valuestring, "NVS_STATS") == 0) {
        log_mqtt(LOG_INFO, TAG, true, "Showing NVS statistics");
        show_nvs_stats();
    } else if (strcmp(cmd->valuestring, "OTA_UPDATE") == 0) {
        log_mqtt(LOG_INFO, TAG, true, "Starting OTA update");
        ota_init();
    } else if (strcmp(cmd->valuestring, "CLEAR_SCREEN") == 0) {
        log_mqtt(LOG_INFO, TAG, true, "Clearing screen");
        screen_full_off();
    } else if (strcmp(cmd->valuestring, "WRITE_SCREEN") == 0) {
        cJSON *text = cJSON_GetObjectItem(root, "text");
        cJSON *x = cJSON_GetObjectItem(root, "x");
        cJSON *page = cJSON_GetObjectItem(root, "page");

        if (cJSON_IsString(text) && cJSON_IsNumber(x) && cJSON_IsNumber(page)) {
            log_mqtt(LOG_INFO, TAG, true, "Write screen: '%s' at x=%d page=%d",
                     text->valuestring, x->valueint, page->valueint);
            ssd1306_draw_string(text->valuestring, x->valueint, page->valueint);
        } else {
            log_mqtt(LOG_ERROR, TAG, true, "Invalid WRITE_SCREEN JSON");
        }
        
    } else {
        log_mqtt(LOG_INFO, TAG, true, "Event unkown");
    }

    cJSON_Delete(root);
}

static void handle_mqtt_controller(const char *data, size_t len) {
    int8_t *payload = (int8_t *)data; /*from -128 to 127*/

    esp_err_t err;
    gamepad_t gamepad;
    err = gamepad_from_buffer(payload, &gamepad);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) getting gamepad from buffer", 
                esp_err_to_name(err));
        return;
    }

    dump_gamepad(&gamepad);

    err = apply_gamepad_commands(&gamepad);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) applying gamepad commands", 
                esp_err_to_name(err));
        return;
    }

}


static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    client = event->client;
    int msg_id;
    char topic[event->topic_len + 1];
    // your_context_t *context = event->context;
    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        //ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        log_mqtt(LOG_INFO, TAG, false, "MQTT_EVENT_CONNECTED");

        msg_id = esp_mqtt_client_subscribe(client, "/commands/#", 0);
        msg_id = esp_mqtt_client_subscribe(client, "windowscontrols/gamepad", 0);
        //ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
        //esp_mqtt_client_publish(client, "/commands/qos1", "commence", 0, 1, 0);
        log_mqtt(LOG_INFO, TAG, true, "Sent subscribe to /commands/# successful, msg_id=%d", msg_id);
        log_mqtt(LOG_INFO, TAG, true, "Sent subscribe to windowscontrols/gamepad successful, msg_id=%d", msg_id);

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
            handle_mqtt_data(event->data, event->data_len);
        } else if (strcmp(topic, "windowscontrols/gamepad") == 0 ){
            handle_mqtt_controller(event->data, event->data_len);
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
        if (mqtt_connected && xQueue != NULL) {
            #if USE_QUEUE_LOGS 
                if (xQueueReceive(xQueue, &m, portMAX_DELAY) == pdTRUE) {
                //log_mqtt(LOG_INFO, TAG, false, "Trying MQQT publish: %s", m.payload);
                    if (esp_mqtt_client_publish(client, m.topic, m.payload, strlen(m.payload), m.qos, m.retain) < 0) {
                        ESP_LOGE(TAG, "MQTT publish failed: %s", m.payload);
                    }
                }
            #else
                //empty the whole queue
                while (xQueueReceive(xQueue, &m, 0) == pdTRUE) {
                    if (esp_mqtt_client_publish(client, m.topic, m.payload,
                                                strlen(m.payload), m.qos, m.retain) < 0) {
                        ESP_LOGE(TAG, "MQTT publish failed: %s", m.payload);
                    }
                }

                // if queue empty, delete task & queue
                if (uxQueueMessagesWaiting(xQueue) == 0) {
                    vQueueDelete(xQueue);
                    xQueue = NULL;
                    ESP_LOGI(TAG, "Queue deleted, all pending logs sent");
                    vTaskDelete(NULL);
                }
            #endif
        }
        vTaskDelay((TickType_t)5);
    }
}

void log_mqtt(mqtt_log_level lvl, const char * tag, bool mqtt, const char* fmt, ...) {
    char buf[256];
    va_list args;

    if (lvl < LOG_LEVEL) {
        return;
    }

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
        #if USE_QUEUE_LOGS
            if (xQueue != NULL) {
                mqtt_msg_t m;
                m.qos = 0;
                m.retain = false;
                snprintf(m.topic, sizeof(m.topic), "/logs/%s", tag);
                snprintf(m.payload, sizeof(m.payload), "[%s] %.200s", tag, buf);
                if (xQueueSend(xQueue, &m, 0) != pdTRUE) {
                    ESP_LOGE(TAG, "Fail to send message to Queue");
                }
            } else {
                ESP_LOGD(TAG, "Queue not initialized");
            }
        #else
            if (!mqtt_connected) {
                if (xQueue != NULL) {
                    mqtt_msg_t m;
                    m.qos = 0;
                    m.retain = false;
                    snprintf(m.topic, sizeof(m.topic), "/logs/%s", tag);
                    snprintf(m.payload, sizeof(m.payload), "[%s] %.200s", tag, buf);
                    if (xQueueSend(xQueue, &m, 0) != pdTRUE) {
                        ESP_LOGE(TAG, "Fail to send message to Queue");
                    }
                } else {
                    ESP_LOGD(TAG, "Queue not initialized");
                }
            } else {
                mqtt_msg_t m;
                m.qos = 0;
                m.retain = false;
                snprintf(m.topic, sizeof(m.topic), "/logs/%s", tag);
                snprintf(m.payload, sizeof(m.payload), "[%s] %.200s", tag, buf);
                if (esp_mqtt_client_publish(client, m.topic, m.payload, strlen(m.payload), m.qos, m.retain) < 0) {
                    ESP_LOGE(TAG, "MQTT publish failed: %s", m.payload);
                }
            }
        #endif
        
    }
}

void mqtt_start()
{

    if (initialized) {
        return;
    }
    initialized = 1;

    esp_err_t err;

/*
    Code to store credentials, uncomment, put yours & flash to encrypt your credentials on NVS.
    Then, you can remove this part.

    const char * mqtt_uri_hivemq = "example_uri";
    const char * mqtt_username_hivemq = "example_username";
    const char * mqtt_password_hivemq = "example_password";
    err = save_nvs_str("MQTT_URI", mqtt_uri_hivemq);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) on saving MQTT_URI to NVS",
            esp_err_to_name(err));
    } else {
        log_mqtt(LOG_INFO, TAG, true, "%s saved to NVS to MQTT_URI",
            mqtt_uri_hivemq);
    }
    err = save_nvs_str("MQTT_USERNAME", mqtt_username_hivemq);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) on saving MQTT_USERNAME to NVS",
            esp_err_to_name(err));
    } else {
        log_mqtt(LOG_INFO, TAG, true, "%s saved to NVS to MQTT_USERNAME",
            mqtt_username_hivemq);
    }
    err = save_nvs_str("MQTT_PASSWORD", mqtt_password_hivemq);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) on saving MQTT_PASSWORD to NVS",
            esp_err_to_name(err));
    } else {
        log_mqtt(LOG_INFO, TAG, true, "%s saved to NVS to MQTT_PASSWORD",
            mqtt_password_hivemq);
    }
*/

    char broker_uri[100];
    char broker_username[32];
    char broker_password[32];
    err = load_nvs_str("MQTT_URI", broker_uri);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) on loading MQTT_URI to NVS",
            esp_err_to_name(err));
    } else {
        log_mqtt(LOG_INFO, TAG, true, "MQTT_URI loaded");
    }
    err = load_nvs_str("MQTT_USERNAME", broker_username);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) on loading MQTT_USERNAME to NVS",
            esp_err_to_name(err));
    }
    err = load_nvs_str("MQTT_PASSWORD", broker_password);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) on loading MQTT_PASSWORD to NVS",
            esp_err_to_name(err));
    }
    

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_uri,
        .credentials.username = broker_username,
        .credentials.authentication.password = broker_password,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .network.disable_auto_reconnect = false,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    esp_mqtt_client_start(client);

    //TODO : error check here
}

void init_queue_mqtt() {

    if (xQueue != NULL) {
        return;
    }

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


