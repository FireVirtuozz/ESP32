#include "ky022.h"
#include "sensors_lib.h"
#include "peripherals/rmt_helper.h"
#include "log_lib.h"
#include <string.h>

#if CONFIG_USE_UDPLIB
#include "udp_lib.h"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "ky022_sensor";

#if CONFIG_USE_KY022

#define KY022_RMT_RESOLUTION_HZ 1000000
#define KY022_RMT_MEM_SYMBOLS 64
#define NEC_ONE_LOW_THRESHOLD_US 1000 // above this, low-duration means bit=1

static rmt_rx_helper_t rx;

static uint32_t decode_nec(const rmt_symbol_word_t *symbols, size_t count) {
    if (count < 33) { // header + 32 bits
        return 0;
    }
    uint32_t code = 0;
    for (size_t i = 1; i <= 32; i++) {
        code <<= 1;
        if (symbols[i].duration1 > NEC_ONE_LOW_THRESHOLD_US) {
            code |= 1;
        }
    }
    return code;
}

static void ky022_task(void *params) {
    (void)params;

    if (rmt_rx_helper_init(&rx, KY022_GPIO, KY022_RMT_RESOLUTION_HZ, KY022_RMT_MEM_SYMBOLS) != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }
    log_msg(TAG, "KY-022 initialized on GPIO %d", KY022_GPIO);

    rmt_symbol_word_t symbols[KY022_RMT_MEM_SYMBOLS];

    while (true) {
        if (rmt_rx_helper_capture(&rx, symbols, sizeof(symbols), 1000, 20000, portMAX_DELAY) == ESP_OK) {
            uint32_t code = decode_nec(symbols, KY022_RMT_MEM_SYMBOLS);
            if (code != 0) {
                header_sensor_t header = {0};
                header.esp_id = (uint8_t)CONFIG_ESP_ID;
                header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
                header.type = SENSOR_TYPE_KY022;
                uint8_t buf[HEADER_SENSOR_SIZE + sizeof(uint32_t)];
                serialize_header(&header, buf);
                memcpy(&buf[HEADER_SENSOR_SIZE], &code, sizeof(uint32_t));

#if CONFIG_USE_UDPLIB
                send_udp_sensor(buf, sizeof(buf));
#endif
            }
        }
    }
}

esp_err_t init_ky022(void) {
    return xTaskCreate(ky022_task, "ky022_task", 2560, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

#else // !CONFIG_USE_KY022

esp_err_t init_ky022(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_KY022
