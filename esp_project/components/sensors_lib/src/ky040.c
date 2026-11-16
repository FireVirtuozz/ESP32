#include "ky040.h"
#include "sensors_lib.h"
#include "peripherals/pcnt_encoder.h"
#include "peripherals/gpio_digital.h"
#include "log_lib.h"

#if CONFIG_USE_UDPLIB
#include "udp_lib.h"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "ky040_sensor";

#if CONFIG_USE_KY040

// Telemetry value encoding, preserved from the original wire format:
// 0 = button released, 1 = button pressed, 2 = rotate left, 3 = rotate right.
#define KY040_VAL_RELEASED 0
#define KY040_VAL_PRESSED  1
#define KY040_VAL_LEFT     2
#define KY040_VAL_RIGHT    3

static pcnt_quadrature_t encoder = {
    .pin_a = KY040_CLK_GPIO, .pin_b = KY040_DT_GPIO,
    .high_limit = 1000, .low_limit = -1000, .glitch_filter_ns = 1000,
};
static gpio_edge_input_t button = { .pin = KY040_SW_GPIO };

static void send_event(uint8_t value) {
    header_sensor_t header = {0};
    header.esp_id = (uint8_t)CONFIG_ESP_ID;
    header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
    header.type = SENSOR_TYPE_KY040;
    uint8_t buf[HEADER_SENSOR_SIZE + 1];
    serialize_header(&header, buf);
    buf[HEADER_SENSOR_SIZE] = value;
#if CONFIG_USE_UDPLIB
    send_udp_sensor(buf, sizeof(buf));
#endif
}

// Polls the quadrature delta at a fixed rate and reports discrete
// left/right events, since the original protocol is event-style rather
// than an absolute position.
static void ky040_rotation_task(void *params) {
    (void)params;

    if (pcnt_quadrature_init(&encoder) != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }
    log_msg(TAG, "KY-040 rotation initialized on GPIO %d/%d", KY040_CLK_GPIO, KY040_DT_GPIO);

    while (true) {
        int32_t delta = 0;
        pcnt_quadrature_drain(&encoder, &delta);

        if (delta > 0) {
            send_event(KY040_VAL_RIGHT);
        } else if (delta < 0) {
            send_event(KY040_VAL_LEFT);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void ky040_button_task(void *params) {
    (void)params;

    if (gpio_edge_input_init(&button, GPIO_INTR_ANYEDGE) != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }
    log_msg(TAG, "KY-040 button initialized on GPIO %d", KY040_SW_GPIO);

    while (true) {
        if (gpio_edge_input_wait(&button, portMAX_DELAY) == ESP_OK) {
            bool pressed;
            gpio_edge_input_read_level(&button, &pressed);
            send_event(pressed ? KY040_VAL_PRESSED : KY040_VAL_RELEASED);
        }
    }
}

esp_err_t init_ky040(void) {
    if (xTaskCreate(ky040_rotation_task, "ky040_rot_task", 2560, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return xTaskCreate(ky040_button_task, "ky040_sw_task", 2048, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

#else // !CONFIG_USE_KY040

esp_err_t init_ky040(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_KY040
