#include "ky018.h"
#include "sensors_lib.h"
#include "peripherals/adc_helper.h"
#include "log_lib.h"
#include <string.h>

#if CONFIG_USE_UDPLIB
#include "udp_lib.h"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "ky018_sensor";

#if CONFIG_USE_KY018

static adc_oneshot_sensor_t sensor = { .unit = KY018_ADC_UNIT, .channel = KY018_ADC_CHANNEL };

static void ky018_task(void *params) {
    (void)params;

    if (adc_oneshot_sensor_init(&sensor, ADC_ATTEN_DB_0) != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }
    log_msg(TAG, "KY-018 initialized on ADC channel %d", KY018_ADC_CHANNEL);

    while (true) {
        int raw = 0;
        if (adc_oneshot_sensor_read_raw(&sensor, &raw) == ESP_OK) {
            int32_t val = raw;

            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_KY018;
            uint8_t buf[HEADER_SENSOR_SIZE + sizeof(int32_t)];
            serialize_header(&header, buf);
            memcpy(&buf[HEADER_SENSOR_SIZE], &val, sizeof(int32_t));

#if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
#endif
        }
        vTaskDelay(pdMS_TO_TICKS(KY018_PERIOD_MS));
    }
}

esp_err_t init_ky018(void) {
    return xTaskCreate(ky018_task, "ky018_task", 2560, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

#else // !CONFIG_USE_KY018

esp_err_t init_ky018(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_KY018
