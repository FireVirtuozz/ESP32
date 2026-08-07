#include "addr_rgb_led.h"
#include <inttypes.h>
#include <esp_err.h>
#include "driver/rmt_tx.h"
#include "log_lib.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "addr_rgb_led_library";

#if CONFIG_USE_BUILTIN_LED_W2812B

#define LED_GPIO             48
#define RMT_CLK_RES_HZ       10000000 // 10MHz clock -> 1 tick = 100ns

// WS2812B bit timings, expressed in RMT ticks
#define T0H  4  // 400ns high
#define T0L  8  // 800ns low
#define T1H  8  // 800ns high
#define T1L  4  // 400ns low

static rmt_channel_handle_t tx_chan = NULL;
static rmt_encoder_handle_t copy_encoder = NULL;

esp_err_t init_ws2812_rmt(void) {
    esp_err_t err;

    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = LED_GPIO,
        .mem_block_symbols = 64, // enough for a single LED (24 bits)
        .resolution_hz = RMT_CLK_RES_HZ,
        .trans_queue_depth = 4,
    };
    err = rmt_new_tx_channel(&tx_chan_config, &tx_chan);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) creating RMT TX channel", esp_err_to_name(err));
        return err;
    }

    rmt_copy_encoder_config_t copy_encoder_config = {};
    err = rmt_new_copy_encoder(&copy_encoder_config, &copy_encoder);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) creating RMT copy encoder", esp_err_to_name(err));
        return err;
    }

    err = rmt_enable(tx_chan);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) enabling RMT channel", esp_err_to_name(err));
        return err;
    }

    log_msg(TAG, "Built-in WS2812B initialized");
    return ESP_OK;
}

esp_err_t set_led_color(uint8_t r, uint8_t g, uint8_t b) {
    // WS2812B expects colors in G-R-B order, packed as a single 24-bit value.
    uint32_t color_val = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;

    // 1 LED = 24 bits = 24 RMT symbols
    rmt_symbol_word_t led_symbols[24];

    for (int i = 0; i < 24; i++) {
        uint32_t bit = (color_val >> (23 - i)) & 0x01;

        if (bit == 0) {
            led_symbols[i] = (rmt_symbol_word_t) {
                .duration0 = T0H, .level0 = 1,
                .duration1 = T0L, .level1 = 0,
            };
        } else {
            led_symbols[i] = (rmt_symbol_word_t) {
                .duration0 = T1H, .level0 = 1,
                .duration1 = T1L, .level1 = 0,
            };
        }
    }

    rmt_transmit_config_t transmit_config = {
        .loop_count = 0, // send once
    };

    esp_err_t err = rmt_transmit(tx_chan, copy_encoder, led_symbols, sizeof(led_symbols), &transmit_config);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) transmitting WS2812B frame", esp_err_to_name(err));
        return err;
    }

    err = rmt_tx_wait_all_done(tx_chan, portMAX_DELAY);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) waiting for WS2812B transmission to complete", esp_err_to_name(err));
        return err;
    }

    log_msg(TAG, "W2812B color set to: %u,%u,%u [RGB]", r, g, b);
    return ESP_OK;
}

#else // !CONFIG_USE_BUILTIN_LED_W2812B

esp_err_t init_ws2812_rmt(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t set_led_color(uint8_t r, uint8_t g, uint8_t b) { (void)r; (void)g; (void)b; return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_BUILTIN_LED_W2812B
