#ifndef PERIPHERALS_RMT_HELPER_H_
#define PERIPHERALS_RMT_HELPER_H_

#include <inttypes.h>
#include <esp_err.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"

// Generic building blocks for RMT-based sensors/actuators: precise
// microsecond-level waveform capture (DHT11, IR receivers) and generation
// (IR emitters, addressable LEDs). The RMT peripheral is the right tool
// whenever a protocol's timing is too precise/tight for a bit-banged GPIO
// implementation to reliably capture or generate.

/**
 * Opaque handle for an RMT RX channel: captures a burst of symbols into a
 * buffer, using a semaphore to signal completion from the ISR callback.
 */
typedef struct {
    rmt_channel_handle_t channel;
    SemaphoreHandle_t done_sem;
} rmt_rx_helper_t;

/**
 * Configure an RMT RX channel on the given GPIO.
 *
 * @param helper             handle to initialize
 * @param gpio_num           input pin to capture on
 * @param resolution_hz      tick resolution (e.g. 1000000 for 1 tick = 1us)
 * @param mem_block_symbols  internal RMT memory block size, in symbols
 */
esp_err_t rmt_rx_helper_init(rmt_rx_helper_t *helper, int gpio_num,
    uint32_t resolution_hz, size_t mem_block_symbols);

/**
 * Start a capture and block until it completes (or times out).
 *
 * @param helper            initialized RX helper
 * @param buffer            caller-allocated symbol buffer
 * @param buffer_size       size of buffer, in bytes
 * @param signal_min_ns     shortest pulse considered valid (noise filter)
 * @param signal_max_ns     longest gap before the capture is considered finished
 * @param wait_ticks        how long to wait for the done semaphore
 * @return ESP_OK if a capture completed, ESP_ERR_TIMEOUT otherwise
 */
esp_err_t rmt_rx_helper_capture(rmt_rx_helper_t *helper, rmt_symbol_word_t *buffer,
    size_t buffer_size, uint32_t signal_min_ns, uint32_t signal_max_ns, TickType_t wait_ticks);

/**
 * Opaque handle for an RMT TX channel with a raw copy encoder — suitable for
 * any protocol where the caller builds the exact symbol sequence itself
 * (e.g. NEC IR frames, WS2812B frames).
 */
typedef struct {
    rmt_channel_handle_t channel;
    rmt_encoder_handle_t encoder;
} rmt_tx_helper_t;

/**
 * Configure an RMT TX channel on the given GPIO, with a raw copy encoder.
 *
 * @param helper             handle to initialize
 * @param gpio_num           output pin to transmit on
 * @param resolution_hz      tick resolution (e.g. 1000000 for 1 tick = 1us)
 * @param mem_block_symbols  internal RMT memory block size, in symbols
 * @param carrier_freq_hz    modulation carrier frequency, or 0 to disable
 *                           (e.g. 38000 for standard IR emitters)
 * @param carrier_duty_cycle carrier duty cycle in [0.0, 1.0], ignored if
 *                           carrier_freq_hz is 0
 */
esp_err_t rmt_tx_helper_init(rmt_tx_helper_t *helper, int gpio_num,
    uint32_t resolution_hz, size_t mem_block_symbols,
    uint32_t carrier_freq_hz, float carrier_duty_cycle);

/**
 * Transmit a raw symbol buffer once (no looping).
 */
esp_err_t rmt_tx_helper_transmit(rmt_tx_helper_t *helper, const rmt_symbol_word_t *symbols, size_t symbol_count);

#endif // PERIPHERALS_RMT_HELPER_H_
