#include "rfid_rc522.h"
#include "sensors_lib.h"
#include "peripherals/spi_helper.h"
#include "log_lib.h"
#include <string.h>

#if CONFIG_USE_UDPLIB
#include "udp_lib.h"
#endif

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "rfid_rc522_sensor";

#if CONFIG_USE_RFID_RC522

// MFRC522 register map (subset actually used here).
#define REG_COMMAND      0x01
#define REG_COM_IEN      0x02
#define REG_COM_IRQ      0x04
#define REG_FIFO_DATA    0x09
#define REG_FIFO_LEVEL   0x0A
#define REG_CONTROL      0x0C
#define REG_BIT_FRAMING  0x0D
#define REG_MODE         0x11
#define REG_TX_CONTROL   0x14
#define REG_TX_ASK       0x15
#define REG_VERSION      0x37

#define CMD_IDLE      0x00
#define CMD_TRANSCEIVE 0x0C
#define CMD_SOFT_RESET 0x0F

#define PICC_REQIDL 0x26
#define PICC_ANTICOLL 0x93

static spi_device_handle_t dev;

static esp_err_t write_reg(uint8_t addr, uint8_t value) {
    uint8_t tx[2] = { (uint8_t)((addr << 1) & 0x7E), value };
    return spi_bus_transfer(dev, tx, NULL, sizeof(tx));
}

static uint8_t read_reg(uint8_t addr) {
    uint8_t tx[2] = { (uint8_t)(((addr << 1) & 0x7E) | 0x80), 0x00 };
    uint8_t rx[2] = {0};
    spi_bus_transfer(dev, tx, rx, sizeof(tx));
    return rx[1];
}

static void set_bitmask(uint8_t addr, uint8_t mask) {
    write_reg(addr, read_reg(addr) | mask);
}

static void clear_bitmask(uint8_t addr, uint8_t mask) {
    write_reg(addr, read_reg(addr) & (~mask));
}

static void antenna_on(void) {
    if (!(read_reg(REG_TX_CONTROL) & 0x03)) {
        set_bitmask(REG_TX_CONTROL, 0x03);
    }
}

/**
 * Send a command frame to the card and read back the response.
 * Faithful to the MFRC522 transceive sequence: fill FIFO, start transceive,
 * poll the IRQ register for completion, drain the FIFO.
 */
static esp_err_t to_card(uint8_t cmd, const uint8_t *send_data, uint8_t send_len,
    uint8_t *back_data, uint8_t *back_len) {
    write_reg(REG_COM_IEN, 0x77);
    clear_bitmask(REG_COM_IRQ, 0x80);
    set_bitmask(REG_FIFO_LEVEL, 0x80); // flush FIFO
    write_reg(REG_COMMAND, CMD_IDLE);

    for (uint8_t i = 0; i < send_len; i++) {
        write_reg(REG_FIFO_DATA, send_data[i]);
    }

    write_reg(REG_COMMAND, cmd);
    if (cmd == CMD_TRANSCEIVE) {
        set_bitmask(REG_BIT_FRAMING, 0x80); // start send
    }

    int timeout = 2000;
    uint8_t irq;
    do {
        irq = read_reg(REG_COM_IRQ);
        timeout--;
    } while (timeout > 0 && !(irq & 0x30));
    clear_bitmask(REG_BIT_FRAMING, 0x80);

    if (timeout == 0 || (irq & 0x01)) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t fifo_len = read_reg(REG_FIFO_LEVEL);
    if (back_data != NULL && back_len != NULL) {
        uint8_t n = (fifo_len < *back_len) ? fifo_len : *back_len;
        for (uint8_t i = 0; i < n; i++) {
            back_data[i] = read_reg(REG_FIFO_DATA);
        }
        *back_len = n;
    }
    return ESP_OK;
}

static void rfid_rc522_task(void *params) {
    (void)params;

    gpio_reset_pin(RC522_PIN_RST);
    gpio_set_direction(RC522_PIN_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(RC522_PIN_RST, 1);

    if (spi_bus_init(SPI2_HOST, RC522_PIN_MISO, RC522_PIN_MOSI, RC522_PIN_CLK) != ESP_OK ||
        spi_helper_add_device(SPI2_HOST, RC522_PIN_CS, 1000000, 0, &dev) != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    write_reg(REG_COMMAND, CMD_SOFT_RESET);
    vTaskDelay(pdMS_TO_TICKS(50));
    write_reg(REG_TX_ASK, 0x40);   // 100% ASK modulation
    write_reg(REG_MODE, 0x3D);     // CRC preset value 0x6363
    antenna_on();

    uint8_t version = read_reg(REG_VERSION);
    log_msg(TAG, "RC522 initialized (version register: 0x%02X)", version);

    while (true) {
        uint8_t req[1] = { PICC_REQIDL };
        uint8_t resp[2];
        uint8_t resp_len = sizeof(resp);
        write_reg(REG_BIT_FRAMING, 0x07);

        if (to_card(CMD_TRANSCEIVE, req, sizeof(req), resp, &resp_len) == ESP_OK) {
            // Card present: run anticollision to read its UID.
            uint8_t anticoll[2] = { PICC_ANTICOLL, 0x20 };
            uint8_t uid[5];
            uint8_t uid_len = sizeof(uid);
            write_reg(REG_BIT_FRAMING, 0x00);

            if (to_card(CMD_TRANSCEIVE, anticoll, sizeof(anticoll), uid, &uid_len) == ESP_OK && uid_len >= 4) {
                header_sensor_t header = {0};
                header.esp_id = (uint8_t)CONFIG_ESP_ID;
                header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
                header.type = SENSOR_TYPE_RFID_RC522;
                // Variable-length payload, matching the original firmware:
                // it sends exactly uid_len bytes, not a fixed size.
                uint8_t buf[HEADER_SENSOR_SIZE + sizeof(uid)];
                serialize_header(&header, buf);
                memcpy(&buf[HEADER_SENSOR_SIZE], uid, uid_len);

#if CONFIG_USE_UDPLIB
                send_udp_sensor(buf, HEADER_SENSOR_SIZE + uid_len);
#endif
                log_msg(TAG, "Card UID: %02X %02X %02X %02X", uid[0], uid[1], uid[2], uid[3]);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500)); // matches original polling period
    }
}

esp_err_t init_rfid_rc522(void) {
    return xTaskCreate(rfid_rc522_task, "rfid_rc522_task", 3584, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

#else // !CONFIG_USE_RFID_RC522

esp_err_t init_rfid_rc522(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_RFID_RC522
