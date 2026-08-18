#include "ds18b20.h"
#include "sensors_lib.h"
#include "log_lib.h"

#if CONFIG_USE_UDPLIB
#include "udp_lib.h"
#endif

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

static const char *TAG = "ds18b20_sensor";

#if CONFIG_USE_DS18B20

static portMUX_TYPE ds_spinlock = portMUX_INITIALIZER_UNLOCKED;

/** Reset pulse + presence detection. Bit-banged with precise us timing. */
static bool ds18b20_reset(void) {
    bool presence = false;

    taskENTER_CRITICAL(&ds_spinlock);

    int64_t start = esp_timer_get_time();
    gpio_set_level(DS18B20_GPIO, 0);
    while ((esp_timer_get_time() - start) < 480);

    gpio_set_level(DS18B20_GPIO, 1);

    start = esp_timer_get_time();
    while ((esp_timer_get_time() - start) < 500) {
        if (gpio_get_level(DS18B20_GPIO) == 0) {
            presence = true;
        }
    }

    taskEXIT_CRITICAL(&ds_spinlock);
    return presence;
}

static void ds18b20_write_bit(bool bit) {
    taskENTER_CRITICAL(&ds_spinlock);
    gpio_set_level(DS18B20_GPIO, 0);

    if (bit) {
        esp_rom_delay_us(6);
        gpio_set_level(DS18B20_GPIO, 1);
        esp_rom_delay_us(64);
    } else {
        esp_rom_delay_us(60);
        gpio_set_level(DS18B20_GPIO, 1);
        esp_rom_delay_us(10);
    }
    taskEXIT_CRITICAL(&ds_spinlock);
}

static bool ds18b20_read_bit(void) {
    bool bit;
    taskENTER_CRITICAL(&ds_spinlock);
    gpio_set_level(DS18B20_GPIO, 0);
    esp_rom_delay_us(2);
    gpio_set_level(DS18B20_GPIO, 1);
    esp_rom_delay_us(10);
    bit = (gpio_get_level(DS18B20_GPIO) != 0);
    taskEXIT_CRITICAL(&ds_spinlock);
    esp_rom_delay_us(50);
    return bit;
}

static void ds18b20_write_byte(uint8_t data) {
    for (int i = 0; i < 8; i++) {
        ds18b20_write_bit((data >> i) & 0x01);
    }
}

static uint8_t ds18b20_read_byte(void) {
    uint8_t value = 0;
    for (int i = 0; i < 8; i++) {
        if (ds18b20_read_bit()) {
            value |= (1 << i);
        }
    }
    return value;
}

/** Maxim/Dallas CRC8 (polynomial x^8 + x^5 + x^4 + 1). */
static uint8_t ds18b20_crc8(const uint8_t *addr, uint8_t len) {
    uint8_t crc = 0;
    while (len--) {
        uint8_t inbyte = *addr++;
        for (uint8_t i = 8; i; i--) {
            uint8_t mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            inbyte >>= 1;
        }
    }
    return crc;
}

static void ds18b20_task(void *params) {
    (void)params;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DS18B20_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(DS18B20_GPIO, 1);

    if (!ds18b20_reset()) {
        log_msg(TAG, "DS18B20 presence pulse not detected");
        vTaskDelete(NULL);
        return;
    }
    log_msg(TAG, "DS18B20 initialized on GPIO %d", DS18B20_GPIO);

    while (true) {
        if (ds18b20_reset()) {
            ds18b20_write_byte(0xCC); // SKIP ROM (single sensor on the bus)
            ds18b20_write_byte(0x44); // CONVERT T

            vTaskDelay(pdMS_TO_TICKS(800)); // 12-bit conversion takes up to 750ms

            if (ds18b20_reset()) {
                ds18b20_write_byte(0xCC);
                ds18b20_write_byte(0xBE); // READ SCRATCHPAD

                uint8_t scratchpad[9];
                for (int i = 0; i < 9; i++) {
                    scratchpad[i] = ds18b20_read_byte();
                }

                if (ds18b20_crc8(scratchpad, 8) == scratchpad[8]) {
                    int16_t raw_temp = (scratchpad[1] << 8) | scratchpad[0];
                    float temp_c = raw_temp * 0.0625f; // 12-bit resolution: 0.0625C/LSB
                    int16_t temp_scaled = (int16_t)(temp_c * 100.0f); // e.g. 24.37C -> 2437

                    header_sensor_t header = {0};
                    header.esp_id = (uint8_t)CONFIG_ESP_ID;
                    header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
                    header.type = SENSOR_TYPE_DS18B20;
                    uint8_t buf[HEADER_SENSOR_SIZE + 2];
                    serialize_header(&header, buf);
                    // Sent big-endian (MSB first), matching the original.
                    buf[HEADER_SENSOR_SIZE] = (uint8_t)((temp_scaled >> 8) & 0xFF);
                    buf[HEADER_SENSOR_SIZE + 1] = (uint8_t)(temp_scaled & 0xFF);

#if CONFIG_USE_UDPLIB
                    send_udp_sensor(buf, sizeof(buf));
#endif
                } else {
                    log_msg(TAG, "DS18B20 CRC mismatch");
                }
            } else {
                log_msg(TAG, "DS18B20 reset failed before read");
            }
        } else {
            log_msg(TAG, "DS18B20 reset failed before convert");
        }
    }
}

esp_err_t init_ds18b20(void) {
    return xTaskCreate(ds18b20_task, "ds18b20_task", 3072, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

#else // !CONFIG_USE_DS18B20

esp_err_t init_ds18b20(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_DS18B20
