#include "ky021.h"
#include "sensors_lib.h"
#include "peripherals/gpio_digital.h"
#include "log_lib.h"

#if CONFIG_USE_UDPLIB
#include "udp_lib.h"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "ky021_sensor";

#if CONFIG_USE_KY021

static gpio_edge_input_t input = { .pin = KY021_GPIO };

static void ky021_task(void *params) {
    (void)params;

    if (gpio_edge_input_init(&input, GPIO_INTR_ANYEDGE) != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }
    log_msg(TAG, "KY-021 initialized on GPIO %d", KY021_GPIO);

    while (true) {
        if (gpio_edge_input_wait(&input, portMAX_DELAY) == ESP_OK) {
            bool magnet_present;
            gpio_edge_input_read_level(&input, &magnet_present);

            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_KY021;
            uint8_t buf[HEADER_SENSOR_SIZE + 1];
            serialize_header(&header, buf);
            buf[HEADER_SENSOR_SIZE] = (uint8_t)magnet_present;

#if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
#endif
        }
    }
}

esp_err_t init_ky021(void) {
    return xTaskCreate(ky021_task, "ky021_task", 2048, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

#else // !CONFIG_USE_KY021

esp_err_t init_ky021(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_KY021
