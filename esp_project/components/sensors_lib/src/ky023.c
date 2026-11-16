#include "ky023.h"
#include "sensors_lib.h"
#include "peripherals/adc_helper.h"
#include "peripherals/gpio_digital.h"
#include "log_lib.h"
#include <string.h>

#if CONFIG_USE_UDPLIB
#include "udp_lib.h"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "ky023_sensor";

#if CONFIG_USE_KY023

static adc_channel_handle_t s_joy_x_handle = NULL;
static adc_channel_handle_t s_joy_y_handle = NULL;

static gpio_edge_input_t button = { .pin = KY023_SW_GPIO };

static void ky023_xy_task(void *params) {
    (void)params;

    log_msg(TAG, "KY-023 XY initialized (X: ch%d, Y: ch%d)", KY023_X_CHANNEL, KY023_Y_CHANNEL);

    while (true) {

        uint32_t raw_x = adc_manager_get_raw(s_joy_x_handle);
        uint32_t raw_y = adc_manager_get_raw(s_joy_y_handle);

#if CONFIG_USE_UDPLIB
        header_sensor_t header = {0};
        header.esp_id = (uint8_t)CONFIG_ESP_ID;
        header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
        header.type = SENSOR_TYPE_KY023_XY;
        uint8_t buf[HEADER_SENSOR_SIZE + 2 * sizeof(uint32_t)];
        serialize_header(&header, buf);
        memcpy(&buf[HEADER_SENSOR_SIZE], &raw_x, sizeof(uint32_t));
        memcpy(&buf[HEADER_SENSOR_SIZE + sizeof(uint32_t)], &raw_y, sizeof(uint32_t));

        send_udp_sensor(buf, sizeof(buf));
#endif
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void ky023_button_task(void *params) {
    (void)params;

    if (gpio_edge_input_init(&button, GPIO_INTR_ANYEDGE) != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }
    log_msg(TAG, "KY-023 button initialized on GPIO %d", KY023_SW_GPIO);

    while (true) {
        if (gpio_edge_input_wait(&button, portMAX_DELAY) == ESP_OK) {
            bool pressed;
            gpio_edge_input_read_level(&button, &pressed);

            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_KY023_SW;
            uint8_t buf[HEADER_SENSOR_SIZE + 1];
            serialize_header(&header, buf);
            buf[HEADER_SENSOR_SIZE] = (uint8_t)pressed;

#if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
#endif
        }
    }
}

esp_err_t init_ky023(void) {

    //register before laucnhing task, ensuring it is well init before getting access to buffer
    s_joy_x_handle = adc_manager_register_channel(KY023_X_CHANNEL);
    s_joy_y_handle = adc_manager_register_channel(KY023_Y_CHANNEL);
    if (s_joy_x_handle == NULL || s_joy_y_handle == NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed to register ADC channel");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(ky023_xy_task, "ky023_xy_task", 2560, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return xTaskCreate(ky023_button_task, "ky023_sw_task", 2048, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

#else // !CONFIG_USE_KY023

esp_err_t init_ky023(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_KY023
