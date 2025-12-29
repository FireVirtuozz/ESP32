#include "otaLib.h"
#include <esp_log.h>
#include <esp_https_ota.h>
#include <esp_crt_bundle.h>

static const char *TAG = "ota_library";

void ota_init(const char* firmware_url)
{
    ESP_LOGI(TAG, "Starting OTA from %s", firmware_url);

    esp_http_client_config_t config = {
        .url = firmware_url,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    esp_https_ota_handle_t ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: 0x%x", err);
        return;
    }

    while (1) {
        err = esp_https_ota_perform(ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
    }

    if (esp_https_ota_is_complete_data_received(ota_handle)) {
        err = esp_https_ota_finish(ota_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "OTA successful, restarting...");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            esp_restart();
        } else {
            ESP_LOGE(TAG, "OTA finish failed: 0x%x", err);
        }
    } else {
        ESP_LOGE(TAG, "OTA incomplete");
    }
}
