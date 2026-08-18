#include "fc33.h"
#include "sensors_lib.h"
#include "peripherals/gpio_digital.h"
#include "log_lib.h"

#if CONFIG_USE_UDPLIB
#include "udp_lib.h"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "fc33_sensor";

#if CONFIG_USE_FC33

static gpio_pulse_counter_t counter = { .pin = FC33_GPIO };

static void fc33_task(void *params) {
    (void)params;

    if (gpio_pulse_counter_init(&counter, GPIO_INTR_NEGEDGE) != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }
    log_msg(TAG, "FC-33 initialized on GPIO %d", FC33_GPIO);

    while (true) {
        uint32_t pulses;
        gpio_pulse_counter_drain(&counter, &pulses);

        header_sensor_t header = {0};
        header.esp_id = (uint8_t)CONFIG_ESP_ID;
        header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
        header.type = SENSOR_TYPE_FC33;
        uint8_t buf[HEADER_SENSOR_SIZE + 1];
        serialize_header(&header, buf);
        buf[HEADER_SENSOR_SIZE] = (uint8_t)pulses;

#if CONFIG_USE_UDPLIB
        send_udp_sensor(buf, sizeof(buf));
#endif
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t init_fc33(void) {
    return xTaskCreate(fc33_task, "fc33_task", 2048, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

#else // !CONFIG_USE_FC33

esp_err_t init_fc33(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_FC33
