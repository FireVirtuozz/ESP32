#include "otaLib.h"
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

static const char *TAG = "ota_library";

static bool ota_running = false;

#define EXAMPLE_FIRMWARE_UPGRADE_URL \
"https://raw.githubusercontent.com/FireVirtuozz/ESP32/main/server/build/server.bin"

#define EXAMPLE_SKIP_COMMON_NAME_CHECK 1
#define EXAMPLE_SKIP_VERSION_CHECK 0
#define EXAMPLE_OTA_RECV_TIMEOUT 5000
#define EXAMPLE_OTA_BUF_SIZE 1024
#define EXAMPLE_USE_CERT_BUNDLE 1
//depends on MBEDTLS_CERTIFICATE_BUNDLE

/* Event handler for catching system events */
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == ESP_HTTPS_OTA_EVENT) {
        switch (event_id) {
            case ESP_HTTPS_OTA_START:
                log_mqtt(LOG_INFO, TAG, true, "OTA started");
                break;
            case ESP_HTTPS_OTA_CONNECTED:
                log_mqtt(LOG_INFO, TAG, true, "Connected to server");
                break;
            case ESP_HTTPS_OTA_GET_IMG_DESC:
                log_mqtt(LOG_INFO, TAG, true, "Reading Image Description");
                break;
            case ESP_HTTPS_OTA_VERIFY_CHIP_ID:
                log_mqtt(LOG_INFO, TAG, true, "Verifying chip id of new image: %d", *(esp_chip_id_t *)event_data);
                break;
            case ESP_HTTPS_OTA_VERIFY_CHIP_REVISION:
                log_mqtt(LOG_INFO, TAG, true, "Verifying chip revision of new image: %d", *(esp_chip_id_t *)event_data);
                break;
            case ESP_HTTPS_OTA_DECRYPT_CB:
                log_mqtt(LOG_INFO, TAG, true, "Callback to decrypt function");
                break;
            case ESP_HTTPS_OTA_WRITE_FLASH:
                log_mqtt(LOG_DEBUG, TAG, true, "Writing to flash: %d written", *(int *)event_data);
                break;
            case ESP_HTTPS_OTA_UPDATE_BOOT_PARTITION:
                log_mqtt(LOG_INFO, TAG, true, "Boot partition updated. Next Partition: %d", *(esp_partition_subtype_t *)event_data);
                break;
            case ESP_HTTPS_OTA_FINISH:
                log_mqtt(LOG_INFO, TAG, true, "OTA finish");
                break;
            case ESP_HTTPS_OTA_ABORT:
                log_mqtt(LOG_INFO, TAG, true, "OTA abort");
                break;
        }
    }
}

static esp_err_t validate_image_header(esp_app_desc_t *new_app_info)
{
    if (new_app_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        log_mqtt(LOG_INFO, TAG, true, "Running firmware version: %s", running_app_info.version);
        log_mqtt(LOG_INFO, TAG, true, "New firmware version: %s", new_app_info->version);
    }

#if !EXAMPLE_SKIP_VERSION_CHECK
    if (memcmp(new_app_info->version, running_app_info.version, sizeof(new_app_info->version)) == 0) {
        log_mqtt(LOG_WARN, TAG, true, "Current running version is the same as a new. We will not continue the update.");
        return ESP_FAIL;
    }
#endif

    return ESP_OK;
}

static esp_err_t _http_client_init_cb(esp_http_client_handle_t http_client)
{
    esp_err_t err = ESP_OK;
    /* Uncomment to add custom headers to HTTP request */
    err = esp_http_client_set_header(http_client, "User-Agent", "ESP32-OTA");
    return err;
}

void advanced_ota_example_task(void *pvParameter)
{
    log_mqtt(LOG_INFO, TAG, true, "Starting Advanced OTA example");

    esp_err_t err;
    esp_err_t ota_finish_err = ESP_OK;
    esp_http_client_config_t config = {
        .url = EXAMPLE_FIRMWARE_UPGRADE_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = EXAMPLE_OTA_RECV_TIMEOUT,
        .keep_alive_enable = true,
        .buffer_size = EXAMPLE_OTA_BUF_SIZE,
        };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
        .http_client_init_cb = _http_client_init_cb,
        };

    esp_https_ota_handle_t https_ota_handle = NULL;
    err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "ESP HTTPS OTA Begin failed");
        ota_running = false;
        vTaskDelete(NULL);
    }

    esp_app_desc_t app_desc = {};
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "esp_https_ota_get_img_desc failed");
        goto ota_end;
    }
    err = validate_image_header(&app_desc);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "image header verification failed");
        goto ota_end;
    }

    while (1) {

        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        // esp_https_ota_perform returns after every read operation which gives user the ability to
        // monitor the status of OTA upgrade by calling esp_https_ota_get_image_len_read, which gives length of image
        // data read so far.
        const size_t len = esp_https_ota_get_image_len_read(https_ota_handle);
        log_mqtt(LOG_DEBUG, TAG, true, "Image bytes read: %d", len);
    }

    if (esp_https_ota_is_complete_data_received(https_ota_handle) != true) {
        // the OTA image was not completely received and user can customise the response to this situation.
        log_mqtt(LOG_ERROR, TAG, true, "Complete data was not received.");
    } else {

        ota_finish_err = esp_https_ota_finish(https_ota_handle);
        
        if ((err == ESP_OK) && (ota_finish_err == ESP_OK)) {
            log_mqtt(LOG_INFO, TAG, true, "ESP_HTTPS_OTA upgrade successful. Rebooting ...");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            esp_restart();
        } else {

            if (ota_finish_err == ESP_ERR_OTA_VALIDATE_FAILED) {
                log_mqtt(LOG_ERROR, TAG, true, "Image validation failed, image is corrupted");
            }

            log_mqtt(LOG_ERROR, TAG, true, "ESP_HTTPS_OTA upgrade failed 0x%x", ota_finish_err);
            ota_running = false;
            vTaskDelete(NULL);
        }
    }

ota_end:
    esp_https_ota_abort(https_ota_handle);
    log_mqtt(LOG_ERROR, TAG, true, "ESP_HTTPS_OTA upgrade failed");
    ota_running = false;
    vTaskDelete(NULL);
}

void ota_init() {

    if (!ota_running) {
    ota_running = true;
    log_mqtt(LOG_INFO, TAG, false, "OTA example app_main start");

    ESP_ERROR_CHECK(esp_event_handler_register(ESP_HTTPS_OTA_EVENT,
        ESP_EVENT_ANY_ID, &event_handler, NULL));

    // esp_wifi_set_ps(WIFI_PS_NONE);

    xTaskCreate(&advanced_ota_example_task,
        "advanced_ota_example_task", 1024 * 8, NULL, 5, NULL);

    } else {
        log_mqtt(LOG_WARN, TAG, true, "OTA already updating");
    }
}
