#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_crc.h"

#include "espnowLib.h"
#include "logLib.h"
#if CONFIG_USE_UDPLIB
#include "udpLib.h"
#else
#include "cmdLib.h"
#endif

#define ESPNOW_MAXDELAY 512
#define ESPNOW_RX_QUEUE_SIZE  10
#define ESPNOW_TX_QUEUE_SIZE  10


static const char* TAG = "espnow_library";

static QueueHandle_t espnow_queue_rx = NULL;
static QueueHandle_t espnow_queue_tx = NULL;
static QueueHandle_t espnow_queue_send = NULL;

static SemaphoreHandle_t espnow_tx_sem = NULL;

static uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

#if CONFIG_USE_UDPLIB
//sensor mac
static uint8_t mac_esp_peer[ESP_NOW_ETH_ALEN] = { 0xB0, 0xCB, 0xD8, 0xE8, 0x68, 0x39 };
#else
//udp mac
static uint8_t mac_esp_peer[ESP_NOW_ETH_ALEN] = { 0xA4, 0xF0, 0x0F, 0x66, 0xB8, 0x51 };
#endif

typedef struct {
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    uint8_t *data;
    int data_len;
} espnow_rx_event_t;

typedef struct {
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    esp_now_send_status_t status;
} espnow_tx_event_t;

static void espnow_deinit();

/* ESPNOW sending or receiving callback function is called in WiFi task.
 * Users should not do lengthy operations from this task. Instead, post
 * necessary data to a queue and handle it from a lower priority task. */
static void espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    espnow_tx_event_t evt = {0};

    if (tx_info == NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Send cb arg error");
        return;
    }
    xSemaphoreGiveFromISR(espnow_tx_sem, NULL);

    memcpy(evt.mac_addr, tx_info->des_addr, ESP_NOW_ETH_ALEN);
    evt.status = status;
    if (xQueueSend(espnow_queue_tx, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        log_msg_lvl(ESP_LOG_WARN, TAG, "Send send queue fail");
    }
}

static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    espnow_rx_event_t evt = {0};
    uint8_t * mac_addr = recv_info->src_addr;
    uint8_t * des_addr = recv_info->des_addr;

    if (mac_addr == NULL || data == NULL || len <= 0) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Receive cb arg error");
        return;
    }

    memcpy(evt.mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    evt.data = malloc(len);
    if (evt.data == NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Malloc receive data fail");
        return;
    }
    memcpy(evt.data, data, len);
    evt.data_len = len;
    if (xQueueSend(espnow_queue_rx, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        log_msg_lvl(ESP_LOG_WARN, TAG, "Send receive queue fail");
        free(evt.data);
    }
}

static void espnow_task_rx(void *pvParameter)
{
    esp_err_t err;
    espnow_rx_event_t evt;

    while (xQueueReceive(espnow_queue_rx, &evt, portMAX_DELAY) == pdTRUE) {
        log_msg(TAG, "Receive unicast data from: "MACSTR", len: %d", MAC2STR(evt.mac_addr), evt.data_len);

        //dispatch msg through UDP when received
        //example: sensor data received from ESP1, redirected to station using UDP
        //if not using UDP, this means that it is ESP1 (sensor)
        //so we apply the commands received
#if CONFIG_USE_UDPLIB
        udp_msg_t msg;
        memcpy(msg.data, evt.data, evt.data_len);
        msg.len = evt.data_len;
        send_udp_msg(&msg);
#else
        //cmd_dispatch((int8_t*)evt.data);
#endif
        free(evt.data);
    }
}

static void espnow_task_tx(void *pvParameter)
{
    espnow_tx_event_t evt;

    //task to monitor that tx is doing well
    while (xQueueReceive(espnow_queue_tx, &evt, portMAX_DELAY) == pdTRUE) {
        log_msg(TAG, "Send data to "MACSTR", status1: %d", MAC2STR(evt.mac_addr), evt.status); 
    }
}

static void espnow_task_send(void *pvParameter)
{
    esp_err_t err;
    espnow_msg_t msg;

    while (xQueueReceive(espnow_queue_send, &msg, portMAX_DELAY) == pdTRUE) {
        xSemaphoreTake(espnow_tx_sem, pdMS_TO_TICKS(100));
        err = esp_now_send(mac_esp_peer, msg.data, msg.len);
        if (err != ESP_OK) {
            log_msg_lvl(ESP_LOG_WARN, TAG, "Error (%s) sending espnow message", esp_err_to_name(err));
        } else {
            log_msg(TAG, "Sending message (len: %u)", msg.len);
        }
    }
}

void espnow_init(void)
{
    esp_err_t err;
    esp_now_peer_info_t peer = {0};

    if (espnow_queue_send != NULL || espnow_queue_rx != NULL || espnow_queue_tx != NULL) {
        log_msg_lvl(ESP_LOG_WARN, TAG, "ESP-NOW already initialized");
        espnow_deinit();
        return;
    }

    espnow_tx_sem = xSemaphoreCreateBinary();
    if (espnow_tx_sem == NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Create tx semaphore fail");
        espnow_deinit();
        return;
    }
    xSemaphoreGive(espnow_tx_sem);

    espnow_queue_rx = xQueueCreate(ESPNOW_RX_QUEUE_SIZE, sizeof(espnow_rx_event_t));
    if (espnow_queue_rx == NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Create rx queue fail");
        espnow_deinit();
        return;
    }

    espnow_queue_tx = xQueueCreate(ESPNOW_TX_QUEUE_SIZE, sizeof(espnow_tx_event_t));
    if (espnow_queue_tx == NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Create tx queue fail");
        espnow_deinit();
        return;
    }

    espnow_queue_send = xQueueCreate(ESPNOW_TX_QUEUE_SIZE, sizeof(espnow_msg_t));
    if (espnow_queue_send == NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Create send queue fail");
        espnow_deinit();
        return;
    }

    /* Initialize ESPNOW and register sending and receiving callback function. */
    err = esp_now_init();
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) while init ESP-NOW", esp_err_to_name(err));
        espnow_deinit();
        return;
    }
    err = esp_now_register_send_cb(espnow_send_cb);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) while register send cb", esp_err_to_name(err));
        espnow_deinit();
        return;
    }
    err = esp_now_register_recv_cb(espnow_recv_cb);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) while register recv cb", esp_err_to_name(err));
        espnow_deinit();
        return;
    }
    /* Set primary master key. Same for two ESPs communicating together.*/
    err = esp_now_set_pmk((uint8_t *)"pmk1234567890123");
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) while setting PMK", esp_err_to_name(err));
        espnow_deinit();
        return;
    }

    peer.channel = 1;
#if CONFIG_USE_AP_MODE
    peer.ifidx = WIFI_IF_AP;
#else
    peer.ifidx = WIFI_IF_AP;
#endif
    peer.encrypt = false;
    memcpy(peer.peer_addr, broadcast_mac, ESP_NOW_ETH_ALEN);
    err = esp_now_add_peer(&peer);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) while adding peer", esp_err_to_name(err));
        espnow_deinit();
        return;
    }

    /* Add ESP sensor peer information to peer list. */
    memset(&peer, 0, sizeof(esp_now_peer_info_t));
    peer.channel = 1;
#if CONFIG_USE_AP_MODE
    peer.ifidx = WIFI_IF_AP;
#else
    peer.ifidx = WIFI_IF_AP;
#endif
    peer.encrypt = false;
    memcpy(peer.peer_addr, mac_esp_peer, ESP_NOW_ETH_ALEN);
    err = esp_now_add_peer(&peer);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) while adding peer", esp_err_to_name(err));
        espnow_deinit();
        return;
    }

    xTaskCreate(espnow_task_rx, "espnow_task_rx", 4096, NULL, 4, NULL);
    xTaskCreate(espnow_task_tx, "espnow_task_tx", 2048, NULL, 4, NULL);
    xTaskCreate(espnow_task_send, "espnow_task_send", 4096, NULL, 4, NULL);

    log_msg(TAG, "ESP-NOW initialized");
}

static void espnow_deinit()
{
    if (espnow_queue_rx) { vQueueDelete(espnow_queue_rx); espnow_queue_rx = NULL; }
    if (espnow_queue_tx) { vQueueDelete(espnow_queue_tx); espnow_queue_tx = NULL; }
    if (espnow_queue_send) { vQueueDelete(espnow_queue_send); espnow_queue_send = NULL; }
    esp_now_deinit();
}

void send_espnow_msg(espnow_msg_t *msg){
    if (espnow_queue_send == NULL || msg == NULL) {
        return;
    }
    xQueueSend(espnow_queue_send, msg, 0);
}