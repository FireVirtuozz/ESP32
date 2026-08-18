#include "ky033.h"
#include "sensors_lib.h"
#include "peripherals/pcnt_encoder.h"
#include "log_lib.h"

#if CONFIG_USE_UDPLIB
#include "udp_lib.h"
#endif
#if CONFIG_USE_LEDLIB
#include "h_bridge.h"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "ky033_sensor";

#if CONFIG_USE_KY033

#define KY033_WINDOW_SIZE 5 // 5 * 20ms = 100ms sliding window

static pcnt_single_channel_t counter = { .pin = KY033_GPIO, .high_limit = 20000, .glitch_filter_ns = 10000 };
static uint16_t window_samples[KY033_WINDOW_SIZE] = {0};
static pcnt_sliding_window_t window = { .samples = window_samples, .size = KY033_WINDOW_SIZE };

static volatile uint16_t pulses_20ms = 0;
static volatile uint16_t pulses_100ms = 0;

static void ky033_task(void *params) {
    (void)params;

    if (pcnt_single_channel_init(&counter) != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }
    log_msg(TAG, "KY-033 initialized on GPIO %d", KY033_GPIO);

    while (true) {
        int32_t raw_count = 0;
        pcnt_single_channel_drain(&counter, &raw_count);

        uint16_t sample = (uint16_t)raw_count;
        pulses_20ms = sample;
        pulses_100ms = (uint16_t)pcnt_sliding_window_push(&window, sample);

        int16_t motor = -1001;
#if CONFIG_USE_LEDLIB
        get_motor_percent(&motor);
#endif
        bool motor_sign_positive = false;
#if CONFIG_USE_LEDLIB
        get_last_motor_sign_positive(&motor_sign_positive);
#endif

        header_sensor_t header = {0};
        header.esp_id = (uint8_t)CONFIG_ESP_ID;
        header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
        header.type = SENSOR_TYPE_KY033;
        uint8_t buf[HEADER_SENSOR_SIZE + 4];
        serialize_header(&header, buf);
        buf[HEADER_SENSOR_SIZE] = (uint8_t)pulses_100ms;
        memcpy(&buf[HEADER_SENSOR_SIZE + 1], &motor, sizeof(int16_t));
        buf[HEADER_SENSOR_SIZE + 3] = (uint8_t)motor_sign_positive;

#if CONFIG_USE_UDPLIB
        send_udp_sensor(buf, sizeof(buf));
#endif
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t init_ky033(void) {
    return xTaskCreate(ky033_task, "ky033_task", 2560, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t get_pulses_count_20ms(uint16_t *count) {
    if (count == NULL) return ESP_ERR_INVALID_ARG;
    *count = pulses_20ms;
    return ESP_OK;
}

esp_err_t get_pulses_count_100ms(uint16_t *count) {
    if (count == NULL) return ESP_ERR_INVALID_ARG;
    *count = pulses_100ms;
    return ESP_OK;
}

#else // !CONFIG_USE_KY033

esp_err_t init_ky033(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t get_pulses_count_20ms(uint16_t *count) { if (count) *count = 0; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t get_pulses_count_100ms(uint16_t *count) { if (count) *count = 0; return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_KY033
