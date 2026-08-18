#include "dht11.h"
#include "sensors_lib.h"
#include "peripherals/rmt_helper.h"
#include "log_lib.h"
#include <string.h>

#if CONFIG_USE_UDPLIB
#include "udp_lib.h"
#endif

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"

static const char *TAG = "dht11_sensor";

#if CONFIG_USE_DHT11

#define DHT11_RMT_RESOLUTION_HZ 1000000 // 1 tick = 1us
#define DHT11_RMT_MEM_SYMBOLS 64
// DHT11 sends 40 bits, each starting with a ~50us low, followed by either
// a ~26-28us high (bit 0) or a ~70us high (bit 1).
#define DHT11_BIT_THRESHOLD_US 50

static rmt_rx_helper_t rx;

// DHT11's start sequence (host pulls the line low, then releases it) needs
// a bidirectional GPIO the RMT channel doesn't own outside of a capture
// window, so it's driven directly here before each RMT capture.
static void send_start_signal(void) {
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT11_GPIO, 0);
    ets_delay_us(18000); // >=18ms low, per datasheet
    gpio_set_level(DHT11_GPIO, 1);
    ets_delay_us(30);
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_INPUT);
}

static bool decode_frame(const rmt_symbol_word_t *symbols, size_t count, uint8_t out[5]) {
    if (count < 40) {
        return false;
    }

    memset(out, 0, 5);
    // Skip the initial response pulses (first 1-2 symbols), decode 40 data bits.
    size_t start = (count > 40) ? (count - 40) : 0;
    for (size_t i = 0; i < 40; i++) {
        uint16_t high_duration = symbols[start + i].duration1;
        uint8_t bit = (high_duration > DHT11_BIT_THRESHOLD_US) ? 1 : 0;
        out[i / 8] = (out[i / 8] << 1) | bit;
    }

    uint8_t checksum = (uint8_t)(out[0] + out[1] + out[2] + out[3]);
    return checksum == out[4];
}

static void dht11_task(void *params) {
    (void)params;

    esp_err_t err = gpio_reset_pin(DHT11_GPIO);
    if (err == ESP_OK) {
        err = rmt_rx_helper_init(&rx, DHT11_GPIO, DHT11_RMT_RESOLUTION_HZ, DHT11_RMT_MEM_SYMBOLS);
    }
    if (err != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }
    log_msg(TAG, "DHT11 initialized on GPIO %d", DHT11_GPIO);

    rmt_symbol_word_t symbols[DHT11_RMT_MEM_SYMBOLS];

    while (true) {
        send_start_signal();

        if (rmt_rx_helper_capture(&rx, symbols, sizeof(symbols), 1000, 200000, pdMS_TO_TICKS(200)) == ESP_OK) {
            uint8_t data[5];
            if (decode_frame(symbols, DHT11_RMT_MEM_SYMBOLS, data)) {
                header_sensor_t header = {0};
                header.esp_id = (uint8_t)CONFIG_ESP_ID;
                header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
                header.type = SENSOR_TYPE_DHT11;
                uint8_t buf[HEADER_SENSOR_SIZE + 2];
                serialize_header(&header, buf);
                buf[HEADER_SENSOR_SIZE] = data[0];     // humidity, integer part
                buf[HEADER_SENSOR_SIZE + 1] = data[2]; // temperature, integer part

#if CONFIG_USE_UDPLIB
                send_udp_sensor(buf, sizeof(buf));
#endif
            } else {
                log_msg(TAG, "Checksum mismatch, discarding frame");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2000)); // DHT11 max sample rate: ~1Hz
    }
}

esp_err_t init_dht11(void) {
    return xTaskCreate(dht11_task, "dht11_task", 3072, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

#else // !CONFIG_USE_DHT11

esp_err_t init_dht11(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_DHT11
