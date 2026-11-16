#include "as5600.h"
#include "sensors_lib.h"
#include "peripherals/i2c_helper.h"
#include "log_lib.h"

#if CONFIG_USE_UDPLIB
#include "udp_lib.h"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "as5600_sensor";

#if CONFIG_USE_AS5600

static i2c_master_dev_handle_t dev;

static void as5600_task(void *params) {
    (void)params;

    if (i2c_bus_add_device(AS5600_I2C_ADDR, 400000, &dev) != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }
    log_msg(TAG, "AS5600 initialized at address 0x%02X", AS5600_I2C_ADDR);

    while (true) {
        uint8_t data_rd[2] = {0};
        if (i2c_bus_read_reg8(dev, AS5600_REG_ANGLE, data_rd, sizeof(data_rd)) == ESP_OK) {
            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_AS5600;
            uint8_t buf[HEADER_SENSOR_SIZE + 2];
            serialize_header(&header, buf);
            memcpy(&buf[HEADER_SENSOR_SIZE], data_rd, 2); // sent raw, as-is

#if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
#endif
        }
        vTaskDelay(pdMS_TO_TICKS(AS5600_PERIOD_MS));
    }
}

esp_err_t init_as5600(void) {
    return xTaskCreate(as5600_task, "as5600_task", 2560, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

#else // !CONFIG_USE_AS5600

esp_err_t init_as5600(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_AS5600
