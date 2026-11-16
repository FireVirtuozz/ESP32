
#include "esp_monitor.h"
#include "sensors_lib.h"
#include "log_lib.h"

#if CONFIG_USE_ESP

#if CONFIG_USE_WIFI
#include "wifi_lib.h"
#endif
#include "esp_system.h"

#if CONFIG_USE_UDPLIB
#include "udp_lib.h"
#endif

#include "driver/temperature_sensor.h"
#include "esp_heap_caps.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "actuators_lib.h"
#include "esp_timer.h"
#include "lwip/stats.h"

static const char* TAG = "esp_monitoring";

#if CONFIG_FREE_RTOS_TASK_DEBUG

#define MAX_TRACKED_TASKS 32

// Structure pour mémoriser l'état d'une tâche au tour précédent
typedef struct {
    UBaseType_t task_number;
    uint32_t last_runtime;
    bool active;
    bool visited; // Pour nettoyer les tâches qui ont été supprimées
} TaskHistory_t;

static void get_detailed_cpu_usage(void) {
    static TaskHistory_t history[MAX_TRACKED_TASKS] = {0};
    
    UBaseType_t uxArraySize = uxTaskGetNumberOfTasks();
    TaskStatus_t *pxTaskStatusArray = malloc(uxArraySize * sizeof(TaskStatus_t));
    
    if (pxTaskStatusArray == NULL) return;

    uint32_t ulTotalRunTime;
    uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalRunTime);
    
    // 1. Calculer le delta du temps global du système
    static uint32_t ulLastTotalRunTime = 0;
    uint32_t ulDeltaTotalTime = ulTotalRunTime - ulLastTotalRunTime;
    ulLastTotalRunTime = ulTotalRunTime;

    // Prépare l'historique pour le marquage
    for (int i = 0; i < MAX_TRACKED_TASKS; i++) {
        history[i].visited = false;
    }

    if (ulDeltaTotalTime > 0) {
        // 2. Parcourir toutes les tâches actives actuelles
        for (UBaseType_t x = 0; x < uxArraySize; x++) {
            UBaseType_t task_num = pxTaskStatusArray[x].xTaskNumber;
            uint32_t current_runtime = pxTaskStatusArray[x].ulRunTimeCounter;
            uint32_t delta_task_time = 0;
            int history_idx = -1;

            // Chercher la tâche dans notre historique
            for (int i = 0; i < MAX_TRACKED_TASKS; i++) {
                if (history[i].active && history[i].task_number == task_num) {
                    history_idx = i;
                    break;
                }
            }

            if (history_idx != -1) {
                // Tâche déjà connue : on calcule le delta
                delta_task_time = current_runtime - history[history_idx].last_runtime;
                history[history_idx].last_runtime = current_runtime;
                history[history_idx].visited = true;
            } else {
                // Nouvelle tâche : on lui trouve une place libre dans l'historique
                for (int i = 0; i < MAX_TRACKED_TASKS; i++) {
                    if (!history[i].active) {
                        history[i].task_number = task_num;
                        history[i].last_runtime = current_runtime;
                        history[i].active = true;
                        history[i].visited = true;
                        break;
                    }
                }
                // Premier tour pour cette tâche, pas encore de delta valide
                delta_task_time = 0; 
            }

            // 3. Calculer le POURCENTAGE CPU réel de la tâche
            // Sur ESP32 (Dual Core), ulDeltaTotalTime correspond au temps d'un cœur.
            // Une tâche ne pouvant tourner que sur un seul cœur à la fois, son max est de 100%.
            float task_cpu_percent = ((float)delta_task_time / (float)ulDeltaTotalTime) * 100.0f;
            if (task_cpu_percent > 100.0f) task_cpu_percent = 100.0f; // Sécurité anti-jitter

            // --- TU AS TES DONNÉES ICI ---
            const char* name = pxTaskStatusArray[x].pcTaskName;
            uint32_t stack = pxTaskStatusArray[x].usStackHighWaterMark;
            
            // Log de débug direct sur le port série (pas de ESP_LOG pour éviter la récursion !)
            log_msg(TAG, "Task: %-12s | CPU: %5.1f%% | Min Stack: %lu bytes", 
                   name, task_cpu_percent, (unsigned long)stack);
        }
    }

    // 4. Nettoyage : si une tâche a disparu de FreeRTOS, on libère son slot dans l'historique
    for (int i = 0; i < MAX_TRACKED_TASKS; i++) {
        if (history[i].active && !history[i].visited) {
            history[i].active = false;
        }
    }

    free(pxTaskStatusArray);
}
#endif

static void get_global_cpu_usage(float *out_core0, float *out_core1) {
    UBaseType_t uxArraySize = uxTaskGetNumberOfTasks();
    TaskStatus_t *pxTaskStatusArray = malloc(uxArraySize * sizeof(TaskStatus_t));
    
    if (pxTaskStatusArray != NULL) {
        uint32_t ulTotalRunTime;
        uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalRunTime);
        
        static uint32_t last_total = 0, last_idle0 = 0, last_idle1 = 0;
        uint32_t current_idle0 = 0, current_idle1 = 0;

        // Trouver les tâches IDLE
        for (UBaseType_t x = 0; x < uxArraySize; x++) {
            if (strcmp(pxTaskStatusArray[x].pcTaskName, "IDLE0") == 0) {
                current_idle0 = pxTaskStatusArray[x].ulRunTimeCounter;
            } else if (strcmp(pxTaskStatusArray[x].pcTaskName, "IDLE1") == 0) {
                current_idle1 = pxTaskStatusArray[x].ulRunTimeCounter;
            }
        }

        uint32_t delta_total = ulTotalRunTime - last_total;
        if (delta_total > 0) {
            uint32_t delta_idle0 = current_idle0 - last_idle0;
            uint32_t delta_idle1 = current_idle1 - last_idle1;

            // Calcul du pourcentage d'utilisation
            *out_core0 = 100.0f * (1.0f - ((float)delta_idle0 / (float)delta_total));
            *out_core1 = 100.0f * (1.0f - ((float)delta_idle1 / (float)delta_total));
        }

        last_total = ulTotalRunTime;
        last_idle0 = current_idle0;
        last_idle1 = current_idle1;

        free(pxTaskStatusArray);
    }
}

#define ESP_PACKET_SIZE (8 * sizeof(uint32_t) + 1 * sizeof(int8_t) + 3 * sizeof(float) \
    + 1 * sizeof(uint8_t) + 2 * sizeof(int16_t))

typedef struct esp_packet_st {
    float esp_deg;
    int8_t rssi;
    uint8_t reset_reason;
    uint32_t free_dram;
    uint32_t free_dram_block;
    uint32_t free_psram;
    uint32_t free_psram_block;
    int16_t angle;
    int16_t motor;
    uint32_t nb_packets;
    float core0;
    float core1;
    uint32_t udp_drop;
    uint32_t ip_drop;
    uint32_t udp_chkerr;  
} esp_packet_t;

static void serialize_esp_packet(esp_packet_t *pkt, uint8_t *buf) {
    uint16_t len = HEADER_SENSOR_SIZE;
    memcpy(&buf[len], &pkt->esp_deg, sizeof(float));
    len += sizeof(float);
    memcpy(&buf[len], &pkt->rssi, sizeof(int8_t));
    len += sizeof(int8_t);
    memcpy(&buf[len], &pkt->reset_reason, sizeof(uint8_t));
    len += sizeof(uint8_t);
    memcpy(&buf[len], &pkt->free_dram, sizeof(uint32_t));
    len += sizeof(uint32_t);
    memcpy(&buf[len], &pkt->free_dram_block, sizeof(uint32_t));
    len += sizeof(uint32_t);
    memcpy(&buf[len], &pkt->free_psram, sizeof(uint32_t));
    len += sizeof(uint32_t);
    memcpy(&buf[len], &pkt->free_psram_block, sizeof(uint32_t));
    len += sizeof(uint32_t);
    memcpy(&buf[len], &pkt->angle, sizeof(uint16_t));
    len += sizeof(uint16_t);
    memcpy(&buf[len], &pkt->motor, sizeof(int16_t));
    len += sizeof(int16_t);
    memcpy(&buf[len], &pkt->nb_packets, sizeof(uint32_t));
    len += sizeof(uint32_t);
    memcpy(&buf[len], &pkt->core0, sizeof(float));
    len += sizeof(float);
    memcpy(&buf[len], &pkt->core1, sizeof(float));
    len += sizeof(float);
    memcpy(&buf[len], &pkt->udp_drop, sizeof(uint32_t));
    len += sizeof(uint32_t);
    memcpy(&buf[len], &pkt->ip_drop, sizeof(uint32_t));
    len += sizeof(uint32_t);
    memcpy(&buf[len], &pkt->udp_chkerr, sizeof(uint32_t));
}

static void print_esp(esp_packet_t *pkt) {
    log_msg(TAG, "ESP Temp: %.3f°C", pkt->esp_deg);
    log_msg(TAG, "STA RSSI: %d dBm", pkt->rssi);
    log_msg(TAG, "Last reset reason: %u", pkt->reset_reason);
    
    // DRAM : Affichage brut + conversion en Ko
    log_msg(TAG, "Free DRAM: %lu bytes (%.2f KB)", (unsigned long)pkt->free_dram, pkt->free_dram / 1024.0f);
    log_msg(TAG, "Max DRAM block: %lu bytes (%.2f KB)", (unsigned long)pkt->free_dram_block, pkt->free_dram_block / 1024.0f);
    
    // PSRAM : Affichage brut + conversion en Mo
    log_msg(TAG, "Free PSRAM: %lu bytes (%.2f MB)", (unsigned long)pkt->free_psram, pkt->free_psram / (1024.0f * 1024.0f));
    log_msg(TAG, "Max PSRAM block: %lu bytes (%.2f MB)", (unsigned long)pkt->free_psram_block, pkt->free_psram_block / (1024.0f * 1024.0f));
    
    log_msg(TAG, "Angle: %u°", pkt->angle);
    log_msg(TAG, "Motor: %d", pkt->motor);
    log_msg(TAG, "Packets received: %lu", (unsigned long)pkt->nb_packets);
    log_msg(TAG, "CPU Usage: Core0: %.3f%%, Core1: %.3f%%", pkt->core0, pkt->core1);
}

static void esp_monitor_task(void *params) {
    esp_err_t err;

#if CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C6
    float tsens_out;
    temperature_sensor_config_t temp_sensor = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    temperature_sensor_handle_t temp_handle = NULL;
    err = temperature_sensor_install(&temp_sensor, &temp_handle);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) installing ESP temperature sensor", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    err = temperature_sensor_enable(temp_handle);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) enabling ESP temperature sensor", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
#else
    log_msg(TAG, "Temperature sensor not supported on this target, skipping");
#endif

    while (true) {

        esp_packet_t esp_pck = {0};
        
#if CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C6
        err = temperature_sensor_get_celsius(temp_handle, &tsens_out);
        if (err != ESP_OK) {
            log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) reading ESP temperature sensor", esp_err_to_name(err));
        } else {
            esp_pck.esp_deg = tsens_out;
        }
#else
        esp_pck.esp_deg = 0.0f; // not supported
#endif

        #if CONFIG_USE_WIFI
        int rssi = 0;
        err = sta_get_rssi(&rssi);
        if (err != ESP_OK) {
            log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) reading STA RSSI", esp_err_to_name(err));
        } else {
            esp_pck.rssi = (int8_t)rssi;
        }
        #endif

        //DRAM + PSRAM on ESP32S3, we can seperate
        //log_msg(TAG, "min free heap size: %u", esp_get_minimum_free_heap_size());
        //log_msg(TAG, "free heap size: %u", esp_get_free_heap_size());

        esp_pck.reset_reason = (uint8_t)esp_reset_reason();

        
        esp_pck.free_dram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        esp_pck.free_dram_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

        esp_pck.free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        esp_pck.free_psram_block = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

        //uint32_t free_iram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_IRAM_8BIT);
        //log_msg(TAG, "free iram: %u", free_iram);

        #if CONFIG_USE_LEDLIB
        uint8_t angle = 0;
        int16_t motor = 0;
        err = get_servo_angle(&angle);
        if (err != ESP_OK) {
            log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) reading ANGLE", esp_err_to_name(err));
        } else {
            esp_pck.angle = angle;
        }
        err = get_motor_percent(&motor);
        if (err != ESP_OK) {
            log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) reading MOTOR", esp_err_to_name(err));
        } else {
            esp_pck.motor = motor;
        }
        #endif

        #if CONFIG_USE_UDPLIB
        uint32_t nb_recv = atomic_load(&nb_packets_received);
        esp_pck.nb_packets = nb_recv;
        #endif

        #if LWIP_STATS
        esp_pck.udp_drop   = lwip_stats.udp.drop;
        esp_pck.ip_drop    = lwip_stats.ip.drop;
        esp_pck.udp_chkerr = lwip_stats.udp.chkerr;
        #endif

        float core0 = 0.0, core1 = 0.0;
        get_global_cpu_usage(&core0, &core1);
        esp_pck.core0 = core0;
        esp_pck.core1 = core1;

        //print_esp(&esp_pck);

        #if CONFIG_FREE_RTOS_TASK_DEBUG
        get_detailed_cpu_usage();
        #endif

        header_sensor_t header = {0};
        header.esp_id = (uint8_t)CONFIG_ESP_ID;
        header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
        header.type = SENSOR_TYPE_ESP;
        uint8_t buf[HEADER_SENSOR_SIZE + ESP_PACKET_SIZE];
        serialize_header(&header, buf);
        serialize_esp_packet(&esp_pck, buf);
        
    #if CONFIG_USE_UDPLIB
        send_udp_sensor(buf, sizeof(buf));
    #endif
        
        vTaskDelay(pdMS_TO_TICKS(500));
        
    }
    vTaskDelete(NULL);
}

esp_err_t init_esp_monitor(void) { 
    return xTaskCreate(esp_monitor_task, "esp_monitor_task", 2048, NULL, 3, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

#else

esp_err_t init_esp_monitor(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif