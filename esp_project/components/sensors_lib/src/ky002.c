#include "ky002.h"
#include "sensors_lib.h"
#include "peripherals/gpio_digital.h"
#include "log_lib.h"

#if CONFIG_USE_UDPLIB
#include "udp_lib.h"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "ky002_sensor";

#if CONFIG_USE_KY002

static gpio_pulse_counter_t counter = { .pin = KY002_GPIO };

static void ky002_task(void *params) {
    (void)params;

    if (gpio_pulse_counter_init(&counter, GPIO_INTR_NEGEDGE) != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }
    // NOTE: see the "!!!" comment in ky002.h — this behaves like FC-33, not
    // like a real KY-002 relay. Preserved as-is; log message corrected from
    // the original's leftover "FC-33 initialized" to at least be self-consistent.
    log_msg(TAG, "KY-002 initialized on GPIO %d (pulse-counting behavior, see ky002.h)", KY002_GPIO);

    while (true) {
        uint32_t pulses;
        gpio_pulse_counter_drain(&counter, &pulses);

        header_sensor_t header = {0};
        header.esp_id = (uint8_t)CONFIG_ESP_ID;
        header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
        header.type = SENSOR_TYPE_KY002;
        uint8_t buf[HEADER_SENSOR_SIZE + 1];
        serialize_header(&header, buf);
        buf[HEADER_SENSOR_SIZE] = (uint8_t)pulses;

#if CONFIG_USE_UDPLIB
        send_udp_sensor(buf, sizeof(buf));
#endif
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t init_ky002(void) {
    return xTaskCreate(ky002_task, "ky002_task", 2048, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

#else // !CONFIG_USE_KY002

esp_err_t init_ky002(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_KY002
