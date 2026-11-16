#include "ky035.h"
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

// NOTE ON NAMING: this driver is under CONFIG_USE_KY035, but the original
// firmware's log message here said "KY-003 initialized" (a leftover from
// copy/pasting the KY003 driver as a starting point). Corrected below.
static const char *TAG = "ky035_sensor";

#if CONFIG_USE_KY035

typedef struct {
    uint64_t signal_count;
    int64_t signal_duration;
} ky035_info_t;

static adc_oneshot_sensor_t sensor = { .unit = KY035_ADC_UNIT, .channel = KY035_ADC_CHANNEL };
static ky035_info_t info = {0};

static void serialize_ky035(const ky035_info_t *ky, uint8_t *buf) {
    uint16_t len = HEADER_SENSOR_SIZE;
    memcpy(&buf[len], &ky->signal_count, sizeof(uint64_t));
    len += sizeof(uint64_t);
    memcpy(&buf[len], &ky->signal_duration, sizeof(int64_t));
}

static void ky035_task(void *params) {
    (void)params;

    if (adc_oneshot_sensor_init(&sensor, ADC_ATTEN_DB_12) != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }
    log_msg(TAG, "KY-035 initialized on ADC channel %d", KY035_ADC_CHANNEL);

    int64_t last_timestamp = 0;
    bool last_state = false;

    while (true) {
        int raw;
        if (adc_oneshot_sensor_read_raw(&sensor, &raw) == ESP_OK) {
            bool current_state = raw < KY035_THRESHOLD_RAW;

            if (current_state && !last_state) {
                int64_t now = esp_timer_get_time();
                if (last_timestamp != 0) {
                    info.signal_count++;
                    info.signal_duration = now - last_timestamp;

                    header_sensor_t header = {0};
                    header.esp_id = (uint8_t)CONFIG_ESP_ID;
                    header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
                    header.type = SENSOR_TYPE_KY035;
                    uint8_t buf[HEADER_SENSOR_SIZE + sizeof(uint64_t) + sizeof(int64_t)];
                    serialize_header(&header, buf);
                    serialize_ky035(&info, buf);

#if CONFIG_USE_UDPLIB
                    send_udp_sensor(buf, sizeof(buf));
#endif
                }
                last_timestamp = now;
            }
            last_state = current_state;
        }

        vTaskDelay(pdMS_TO_TICKS(KY035_POLL_PERIOD_MS));
    }
}

esp_err_t init_ky035(void) {
    return xTaskCreate(ky035_task, "ky035_task", 2560, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

#else // !CONFIG_USE_KY035

esp_err_t init_ky035(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_KY035
