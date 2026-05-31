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

static void espnow_header_serialize(header_espnow_frame_t *hd, uint8_t *data) {
    data[0] = hd->flags;
    data[1] = hd->packet_type;
    data[2] = hd->needs_frag;
}

static void espnow_header_deserialize(header_espnow_frame_t *hd, uint8_t *data) {
    hd->flags = data[0];
    hd->packet_type = data[1];
    hd->needs_frag = data[2];
}

#define PKT_TYPE_DIRECT     0
#define PKT_TYPE_FRAGMENT   1

#define HEADER_ESPNOW_FRAG_SIZE (sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint8_t))
#define MAX_FRAG_PAYLOAD_SIZE (ESPNOW_MSG_SIZE - HEADER_ESPNOW_FRAG_SIZE - HEADER_ESPNOW_SIZE) //-1 for type pkt type

typedef struct header_frag_st {
    uint32_t frag_id;
    uint8_t frag_total;
    uint8_t frag_idx;
} header_frag_t;

static void header_fragment_serialize(const header_frag_t *hdr, uint8_t *buf) {
    buf[0] = (hdr->frag_id >> 24) & 0xFF;
    buf[1] = (hdr->frag_id >> 16) & 0xFF;
    buf[2] = (hdr->frag_id >> 8)  & 0xFF;
    buf[3] =  hdr->frag_id        & 0xFF;
    buf[4] = hdr->frag_total;
    buf[5] = hdr->frag_idx;
}

static void header_fragment_deserialize(header_frag_t *hdr, const uint8_t *buf) {
    hdr->frag_id    = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
    hdr->frag_total = buf[4];
    hdr->frag_idx = buf[5];
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

#define DEFRAG_BUFFER_MAX_SIZE 2048

typedef struct {
    uint8_t buffer[DEFRAG_BUFFER_MAX_SIZE];
    uint32_t current_frag_id;
    uint32_t bytes_received;
    uint8_t frags_received_count;
} defrag_ctx_t;

static void espnow_task_rx(void *pvParameter)
{
    esp_err_t err;
    espnow_rx_event_t evt;
    defrag_ctx_t defrag_ctx = {0};

    while (xQueueReceive(espnow_queue_rx, &evt, portMAX_DELAY) == pdTRUE) {
        log_msg(TAG, "Receive unicast data from: "MACSTR", len: %d", MAC2STR(evt.mac_addr), evt.data_len);

        header_espnow_frame_t hd = {0};
        espnow_header_deserialize(&hd, evt.data);
        if (!hd.needs_frag)
        {

    #if CONFIG_USE_UDPLIB
            //udp dispatch
            if (hd.flags & 0b00000001) {
                switch (hd.packet_type) {
                    case DUMP: 
                        send_udp_dump(&evt.data[HEADER_ESPNOW_SIZE], evt.data_len - HEADER_ESPNOW_SIZE);
                        break;
                    case LOGS: 
                        send_udp_log(&evt.data[HEADER_ESPNOW_SIZE], evt.data_len - HEADER_ESPNOW_SIZE);
                        break;
                    case SENSORS: 
                        send_udp_sensor(&evt.data[HEADER_ESPNOW_SIZE], evt.data_len - HEADER_ESPNOW_SIZE);
                        break;
                    case VIDEO: 
                        send_udp_jpeg(&evt.data[HEADER_ESPNOW_SIZE], evt.data_len - HEADER_ESPNOW_SIZE);
                        break;
                    default:
                        break;
                }
            }
    #endif
            switch (hd.packet_type) {
                case DUMP: 
                    break;
                case LOGS: 
                    break;
                case SENSORS: 
                    break;
                case VIDEO: 
                    break;
                case CMD:
                    //cmd_dispatch(&evt.data[HEADER_ESPNOW_SIZE]);
                    break;
                default:
                    break;
            }

        } else {
            
            header_frag_t hd_frag = {0};
            header_fragment_deserialize(&hd_frag, &evt.data[HEADER_ESPNOW_SIZE]);

            const uint8_t *payload = &evt.data[HEADER_ESPNOW_FRAG_SIZE + HEADER_ESPNOW_SIZE];
            int payload_len = evt.data_len - HEADER_ESPNOW_SIZE - HEADER_ESPNOW_FRAG_SIZE;

            if (hd_frag.frag_idx == 0) {
                defrag_ctx.current_frag_id = hd_frag.frag_id;
                defrag_ctx.bytes_received = 0;
                defrag_ctx.frags_received_count = 0;
            }

            if (defrag_ctx.current_frag_id != hd_frag.frag_id) {
                log_msg_lvl(ESP_LOG_WARN, TAG, "Orphaned or out-of-sync fragment dropped (ID: %u)", hd_frag.frag_id);
                break;
            }

            uint32_t write_offset = hd_frag.frag_idx * MAX_FRAG_PAYLOAD_SIZE;
            if ((write_offset + payload_len) > DEFRAG_BUFFER_MAX_SIZE) {
                log_msg_lvl(ESP_LOG_ERROR, TAG, "Defrag buffer overflow prevented! Packet too large.");
                defrag_ctx.current_frag_id = 0;
                break;
            }
            memcpy(&defrag_ctx.buffer[write_offset], payload, payload_len);

            defrag_ctx.bytes_received += payload_len;
            defrag_ctx.frags_received_count++;

            if (defrag_ctx.frags_received_count == hd_frag.frag_total) {
    #if CONFIG_USE_UDPLIB
            //udp dispatch
            if (hd.flags & 0b00000001) {
                switch (hd.packet_type) {
                    case DUMP: 
                        send_udp_dump(defrag_ctx.buffer, defrag_ctx.bytes_received);
                        break;
                    case LOGS: 
                        send_udp_log(defrag_ctx.buffer, defrag_ctx.bytes_received);
                        break;
                    case SENSORS: 
                        send_udp_sensor(defrag_ctx.buffer, defrag_ctx.bytes_received);
                        break;
                    case VIDEO: 
                        send_udp_jpeg(defrag_ctx.buffer, defrag_ctx.bytes_received);
                        break;
                    default:
                        break;
                }
            }
    #endif
                defrag_ctx.current_frag_id = 0;
            }

            break;
        }
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

static void espnow_send_fragmented(espnow_msg_t *msg, uint32_t *running_frag_id) {
    uint32_t total_size_payload = msg->len;
    header_frag_t hd = {0};
    uint8_t buf[ESPNOW_MSG_SIZE];
    
    hd.frag_id = *running_frag_id;
    hd.frag_total = (total_size_payload + MAX_FRAG_PAYLOAD_SIZE - 1) / MAX_FRAG_PAYLOAD_SIZE;

    for (uint16_t i = 0; i < hd.frag_total; i++) {
        uint32_t offset = i * MAX_FRAG_PAYLOAD_SIZE;
        hd.frag_idx = i;
        uint16_t payload_size = (i == hd.frag_total - 1) ? (total_size_payload - offset) : MAX_FRAG_PAYLOAD_SIZE;

        header_fragment_serialize(&hd, &buf[HEADER_ESPNOW_SIZE]);
        memcpy(&buf[HEADER_ESPNOW_SIZE + HEADER_ESPNOW_FRAG_SIZE], msg->data + offset, payload_size);
        
        int err = esp_now_send(mac_esp_peer, buf, payload_size + HEADER_ESPNOW_FRAG_SIZE);
        if (err != ESP_OK) {
            //using serial because otherwise infinite loop of same message
            ESP_LOGW(TAG, "Error (%s) sending espnow message", esp_err_to_name(err));
        } else {
            ESP_LOGD(TAG, "Sending message (len: %u)", payload_size);
        }
    }

    (*running_frag_id)++;
}

static void espnow_task_send(void *pvParameter)
{
    esp_err_t err;
    espnow_msg_t msg;
    uint32_t local_frag_id = 0;

    while (xQueueReceive(espnow_queue_send, &msg, portMAX_DELAY) == pdTRUE) {
        //check if message needs fragmentation
        if (msg.len > ESPNOW_MSG_SIZE) {
            espnow_send_fragmented(&msg, &local_frag_id);
        } else {
            xSemaphoreTake(espnow_tx_sem, pdMS_TO_TICKS(100));
            err = esp_now_send(mac_esp_peer, msg.data, msg.len);
            if (err != ESP_OK) {
                //using serial because otherwise infinite loop of same message
                ESP_LOGW(TAG, "Error (%s) sending espnow message", esp_err_to_name(err));
            } else {
                ESP_LOGD(TAG, "Sending message (len: %u)", msg.len);
            }
        }
        free(msg.data);
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
    //todo : free all data in queues
    if (espnow_queue_rx) { vQueueDelete(espnow_queue_rx); espnow_queue_rx = NULL; }
    if (espnow_queue_tx) { vQueueDelete(espnow_queue_tx); espnow_queue_tx = NULL; }
    if (espnow_queue_send) { vQueueDelete(espnow_queue_send); espnow_queue_send = NULL; }
    esp_now_deinit();
}

void send_espnow_msg(header_espnow_frame_t *hd, const uint8_t * data, uint32_t len){

    if (data == NULL || espnow_queue_send == NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Invalid args send espnow");
        return;
    }
    hd->needs_frag = (len + HEADER_ESPNOW_SIZE) > ESPNOW_MSG_SIZE;

    uint8_t *buf_cpy = malloc(len + HEADER_ESPNOW_SIZE);
    if (buf_cpy != NULL) {

        espnow_header_serialize(hd, buf_cpy);
        memcpy(buf_cpy + HEADER_ESPNOW_SIZE, data, len);

        espnow_msg_t msg = {0};
        msg.data = buf_cpy;
        msg.len = len + HEADER_ESPNOW_SIZE;
    
        if (xQueueSend(espnow_queue_send, &msg, 0) != pdTRUE) {
            log_msg_lvl(ESP_LOG_WARN, TAG, "Queue full, freeing data");
            free(msg.data);
        }
    } else {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed allocating buf cpy");
    }
}