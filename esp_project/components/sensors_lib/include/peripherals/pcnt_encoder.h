#ifndef PERIPHERALS_PCNT_ENCODER_H_
#define PERIPHERALS_PCNT_ENCODER_H_

#include <inttypes.h>
#include <esp_err.h>
#include "driver/pulse_cnt.h"
#include <soc/gpio_num.h>

// Generic building blocks for hardware pulse-counting (PCNT) sensors:
// single-channel speed sensors (optical/hall encoders with one output) and
// quadrature rotary encoders (two outputs, direction-aware).

/**
 * Opaque handle for a single-channel PCNT counter (e.g. an optical speed
 * sensor with only one digital output — no direction information).
 */
typedef struct {
    pcnt_unit_handle_t unit;
    gpio_num_t pin;
    int32_t high_limit;
    uint32_t glitch_filter_ns;
} pcnt_single_channel_t;

/**
 * Configure a single-channel PCNT unit: counts edges on one GPIO, direction-less.
 * Applies a glitch filter to reject spurious short pulses.
 */
esp_err_t pcnt_single_channel_init(pcnt_single_channel_t *counter);

/**
 * Atomically read and reset the raw pulse count since the last call.
 */
esp_err_t pcnt_single_channel_drain(pcnt_single_channel_t *counter, int32_t *count);

/**
 * Fixed-size sliding window used to compute a rolling sum of pulse counts
 * over N recent samples (e.g. turning 5 samples of 20ms each into a
 * continuously-refreshed 100ms estimate, refreshed every 20ms instead of
 * only every 100ms).
 */
typedef struct {
    uint16_t *samples;   // caller-allocated buffer of `size` elements
    uint8_t size;
    uint8_t index;
} pcnt_sliding_window_t;

/**
 * Push a new sample into the sliding window and return the updated rolling sum.
 */
uint32_t pcnt_sliding_window_push(pcnt_sliding_window_t *window, uint16_t new_sample);

/**
 * Opaque handle for a quadrature (2-channel) PCNT rotary encoder.
 * Direction-aware: the count increases or decreases depending on which
 * channel leads the other, decoded entirely in hardware.
 */
typedef struct {
    pcnt_unit_handle_t unit;
    gpio_num_t pin_a;
    gpio_num_t pin_b;
    int32_t high_limit;
    int32_t low_limit;
    uint32_t glitch_filter_ns;
} pcnt_quadrature_t;

/**
 * Configure a 2-channel quadrature PCNT unit (e.g. a KY-040-style rotary
 * encoder's CLK/DT pair). Both edges of both channels are counted for
 * maximum resolution, direction decoded from the level of the other channel.
 */
esp_err_t pcnt_quadrature_init(pcnt_quadrature_t *encoder);

/**
 * Atomically read and reset the signed relative position since the last call.
 * Positive values indicate one rotation direction, negative the other.
 */
esp_err_t pcnt_quadrature_drain(pcnt_quadrature_t *encoder, int32_t *delta);

#endif // PERIPHERALS_PCNT_ENCODER_H_
