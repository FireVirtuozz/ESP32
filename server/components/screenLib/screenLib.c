#include "screenLib.h"
#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_system.h"
#include "esp_log.h"
#include "mqttLib.h"
#include "nvsLib.h"

// I2C pins
#define I2C_MASTER_SDA_IO 22
#define I2C_MASTER_SCL_IO 21

// SSD1306 address
#define OLED_ADDR_DEFAULT 0x3C

static const char * TAG = "screen_library";

static uint8_t screen[128*8];

static SemaphoreHandle_t xMutex = NULL;

static bool i2c_initialized = false;

// Police 5x8 pour A-Z (chaque caractère = 5 colonnes)
const uint8_t font5x8[64][5] = {
    {0x7C,0x12,0x11,0x12,0x7C}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x09,0x01}, // F
    {0x3E,0x41,0x49,0x49,0x7A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x0C,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x3F,0x40,0x38,0x40,0x3F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43},  // Z

    // a-z (26-51)
    {0x20,0x54,0x54,0x54,0x78}, // a
    {0x7F,0x48,0x44,0x44,0x38}, // b
    {0x38,0x44,0x44,0x44,0x20}, // c
    {0x38,0x44,0x44,0x48,0x7F}, // d
    {0x38,0x54,0x54,0x54,0x18}, // e
    {0x08,0x7E,0x09,0x01,0x02}, // f
    {0x0C,0x52,0x52,0x52,0x3E}, // g
    {0x7F,0x08,0x04,0x04,0x78}, // h
    {0x00,0x44,0x7D,0x40,0x00}, // i
    {0x20,0x40,0x44,0x3D,0x00}, // j
    {0x7F,0x10,0x28,0x44,0x00}, // k
    {0x00,0x41,0x7F,0x40,0x00}, // l
    {0x7C,0x04,0x18,0x04,0x78}, // m
    {0x7C,0x08,0x04,0x04,0x78}, // n
    {0x38,0x44,0x44,0x44,0x38}, // o
    {0x7C,0x14,0x14,0x14,0x08}, // p
    {0x08,0x14,0x14,0x18,0x7C}, // q
    {0x7C,0x08,0x04,0x04,0x08}, // r
    {0x48,0x54,0x54,0x54,0x20}, // s
    {0x04,0x3F,0x44,0x40,0x20}, // t
    {0x3C,0x40,0x40,0x20,0x7C}, // u
    {0x1C,0x20,0x40,0x20,0x1C}, // v
    {0x3C,0x40,0x30,0x40,0x3C}, // w
    {0x44,0x28,0x10,0x28,0x44}, // x
    {0x0C,0x50,0x50,0x50,0x3C}, // y
    {0x44,0x64,0x54,0x4C,0x44}, // z

    // 0-9 (52-61)
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E},  // 9

    // 62 : .
    {0x00, 0x60, 0x60, 0x00, 0x00},

    // 63 : :
    {0x00, 0x36, 0x36, 0x00, 0x00}
};

/*
================================================================
INIT I2C BUS : master (esp32) and device (ssd1306)
================================================================
*/

//intern pointer : active master i2c bus
i2c_master_bus_handle_t bus_handle;

//describe i2c bus
i2c_master_bus_config_t i2c_mst_config = {
    .clk_source = I2C_CLK_SRC_DEFAULT,  //clock source for i2c bus (default)
    .i2c_port = I2C_NUM_0, //i2c number of esp32
    .scl_io_num = I2C_MASTER_SCL_IO, // pin for scl (clock), 21 here
    .sda_io_num = I2C_MASTER_SDA_IO, // pin for sda (data), 22 here
    .glitch_ignore_cnt = 7, //ignore short impulsions (for noise)
    .flags.enable_internal_pullup = true,
};

//pointer towards i2c device
i2c_master_dev_handle_t dev_handle;

//describe an i2c device
i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7, //length of i2c address
    .device_address = OLED_ADDR_DEFAULT, //address of device
    .scl_speed_hz = 400000, //i2c bus frequency
};

static void i2c_scan() {
    esp_err_t err;
    log_mqtt(LOG_INFO, TAG, true, "Starting I2C scan...");

    for (uint8_t addr = 0x03; addr <= 0x77; addr++) { // valid range
        err = i2c_master_probe(bus_handle, addr, 1000 / portTICK_PERIOD_MS);
        if (err == ESP_OK) {
            log_mqtt(LOG_INFO, TAG, true, "Device found at 0x%02X", addr);
        }
    }

    log_mqtt(LOG_INFO, TAG, true, "I2C scan finished");
}

/*
================================================================
TRANSMIT DATA : master -> device
================================================================
*/

/**
 * @brief Send commands to SSD1306 over I2C
 * 
 * @param handle        I2C device handle
 * @param cmd           Pointer to command buffer
 * @param cmd_size      Number of command bytes
 * @return esp_err_t    ESP_OK if successful, error code otherwise
 */
static esp_err_t ssd1306_send_cmd(i2c_master_dev_handle_t handle,
                        uint8_t *cmd, size_t cmd_size)
{
    if (!cmd || cmd_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t control_byte = 0x00; // 0x00 = commands
    i2c_master_transmit_multi_buffer_info_t multi_buffer[2] = {
        {.write_buffer = &control_byte, .buffer_size = 1},
        {.write_buffer = cmd,           .buffer_size = cmd_size}
    };

    return i2c_master_multi_buffer_transmit(handle, multi_buffer, 2, -1);
}

/**
 * @brief Send data/pixels to SSD1306 over I2C
 * 
 * @param handle        I2C device handle
 * @param data          Pointer to data buffer
 * @param data_size     Number of bytes
 * @return esp_err_t    ESP_OK if successful, error code otherwise
 */
static esp_err_t ssd1306_send_data(i2c_master_dev_handle_t handle,
                            uint8_t *data, size_t data_size)
{
    if (!data || data_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t control_byte = 0x40; // 0x40 = data
    i2c_master_transmit_multi_buffer_info_t multi_buffer[2] = {
        {.write_buffer = &control_byte, .buffer_size = 1},
        {.write_buffer = data,         .buffer_size = data_size}
    };

    return i2c_master_multi_buffer_transmit(handle, multi_buffer, 2, -1);
}

static void ssd1306_flush_screen()
{
    for (int page = 0; page < 8; page++) {
        uint8_t cmd[] = {0xB0 | page, 0x00, 0x10};
        esp_err_t err = ssd1306_send_cmd(dev_handle, cmd, sizeof(cmd));
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) sending command", esp_err_to_name(err));
            return;
        }

        err = ssd1306_send_data(dev_handle, &screen[page * 128], 128);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) sending data", esp_err_to_name(err));
            return;
        }
    }
    //save_nvs_blob("screen", screen, sizeof(screen));
}

// transmit one buffer data : sending 5 commands in a row
// 0x00 : SSD1306 control byte, command type
// 0xAE, 0xA6, 0x20, 0xAF = SSD1306 commands
//uint8_t data_wr[5] = {0x00, 0xAE, 0xA6, 0x20, 0xAF};
//ESP_ERROR_CHECK(i2c_master_transmit(dev_handle, data_wr, 5, -1));

static void ssd1306_draw_char(char c, int x, int page) {
    int index = -1;

    if (c >= 'A' && c <= 'Z') {
        index = c - 'A';             // 0-25
    } else if (c >= 'a' && c <= 'z') {
        index = 26 + (c - 'a');      // 26-51
    } else if (c >= '0' && c <= '9') {
        index = 52 + (c - '0');      // 52-61
    } else if (c == '.') index = 62;
    else if (c == ':') index = 63;

    if (index == -1) return; // caractère non supporté

    for (int i = 0; i < 5; i++) {
        screen[page * 128 + x + i] = font5x8[index][i];
    }
}

void ssd1306_draw_string(const char *str, int x, int page) {
    if (xMutex == NULL) {
        log_mqtt(LOG_ERROR, TAG, true, "Error on mutex creation");
        return;
    }

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        log_mqtt(LOG_DEBUG, TAG, true, "Drawing : %s, offset %d, page %d", str, x, page); 
        uint8_t a = 0;
        while (a < 128) {
            screen[page * 128 + a] = 0x00;
            a++;
        }
        while (*str) {
            char c = *str++;
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9') || c == '.' || c == ':') {
                ssd1306_draw_char(c, x, page);
            }
            x += 6; // 5 pixels de largeur + 1 pixel d'espacement
            if (x > 127) break;
        }

        ssd1306_flush_screen();
        xSemaphoreGive(xMutex);
    }
}

// --------- I2C Init & Scan ---------
static void i2c_init()
{
    if (i2c_initialized) {
        log_mqtt(LOG_WARN, TAG, true, "I2C already initialized");
        return;
    }

    i2c_initialized = true;

    //config of i2c registers, gpio, start
    esp_err_t err = i2c_new_master_bus(&i2c_mst_config, &bus_handle);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) allocating I²C master bus", esp_err_to_name(err));
        return;
    }

    i2c_scan();

    //check if the adress is known
    err = i2c_master_probe(bus_handle, OLED_ADDR_DEFAULT, -1);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) finding screen address", esp_err_to_name(err));
        return;
    }

    //add the address of the device on the master bus
    err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) adding device to master bus", esp_err_to_name(err));
        return;
    }

    log_mqtt(LOG_INFO, TAG, true, "I²C initialized");
}

void ssd1306_setup()
{

    if( xMutex != NULL )
    {
        log_mqtt(LOG_WARN, TAG, true, "Mutex already initialized");
        return;
    }

    xMutex = xSemaphoreCreateMutex();

    if( xMutex == NULL )
    {
        log_mqtt(LOG_ERROR, TAG, true, "Error on mutex creation");
        return;
    }

    i2c_init();

    uint8_t init_cmds[] = {
        0xAE,       // Display OFF
        0x20, 0x00, // Horizontal addressing mode
        0xB0,       // Page start address 0
        0xC8,       // COM output scan direction remapped
        0x00,       // Lower column address
        0x10,       // Higher column address
        0x40,       // Display start line
        0x81, 0x7F, // Contrast
        0xA1,       // Segment remap
        0xA6,       // Normal display
        0xA8, 0x3F, // Multiplex ratio
        0xD3, 0x00, // Display offset
        0xD5, 0x80, // Display clock divide / freq
        0xD9, 0xF1, // Pre-charge period
        0xDA, 0x12, // COM pins config
        0xDB, 0x40, // VCOMH deselect level
        0x8D, 0x14, // Charge pump
        0xAF        // Display ON
    };

    esp_err_t err = ssd1306_send_cmd(dev_handle, init_cmds, sizeof(init_cmds));
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) sending command", esp_err_to_name(err));
        return;
    }

    memset(screen, 0x00, sizeof(screen));
    load_nvs_blob("screen", screen, sizeof(screen));
    ssd1306_flush_screen();

    log_mqtt(LOG_INFO, TAG, true, "ssd1306 initialized");
}

void screen_full_on() {
    if (xMutex == NULL) {
        return;
    }

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        memset(screen, 0xFF, sizeof(screen));   //pixels on
        ssd1306_flush_screen();
        log_mqtt(LOG_INFO, TAG, true, "Screen full on set");
        xSemaphoreGive(xMutex);
    }
}

void screen_full_off() {
    if (xMutex == NULL) {
        return;
    }

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        memset(screen, 0x00, sizeof(screen));   //pixels off
        ssd1306_flush_screen();
        log_mqtt(LOG_INFO, TAG, true, "Screen full off set");
        xSemaphoreGive(xMutex);
    }
}