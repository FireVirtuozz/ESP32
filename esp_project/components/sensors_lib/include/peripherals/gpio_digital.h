#ifndef PERIPHERALS_GPIO_DIGITAL_H_
#define PERIPHERALS_GPIO_DIGITAL_H_

#include <inttypes.h>
#include <stdbool.h>
#include <esp_err.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"

// Generic building blocks for simple digital-input sensors (buttons, tilt
// switches, knock sensors, reed switches, PIR/microwave motion outputs...).
// Two patterns cover essentially every simple digital sensor in this project:
//
// 1. "Edge state" sensors: an edge wakes a task, which reads back the level
//    and reports it (used by tilt/knock/reed/button/motion-style sensors).
// 2. "Pulse counter" sensors: an edge increments a counter in an ISR-safe
//    way; a periodic task drains the counter and reports the pulse count
//    (used by hall-effect/speed-style sensors read purely digitally).

/**
 * Opaque handle for a GPIO "edge state" input: an ISR gives a semaphore on
 * each edge, and gpio_digital_wait_edge() blocks until the next one.
 */
typedef struct {
    gpio_num_t pin;
    SemaphoreHandle_t sem;
} gpio_edge_input_t;

/**
 * Configure a GPIO as an edge-triggered digital input.
 * Installs the shared GPIO ISR service if not already installed
 * (ESP_ERR_INVALID_STATE from a prior install by another sensor is treated
 * as success, since the service is a singleton shared across the whole app).
 *
 * @param input      handle to initialize (input->pin must already be set)
 * @param intr_type  GPIO_INTR_POSEDGE, GPIO_INTR_NEGEDGE or GPIO_INTR_ANYEDGE
 */
esp_err_t gpio_edge_input_init(gpio_edge_input_t *input, gpio_int_type_t intr_type);

/**
 * Block until the next edge occurs on this input (or forever if timeout_ticks
 * is portMAX_DELAY).
 *
 * @return ESP_OK if an edge occurred, ESP_ERR_TIMEOUT otherwise
 */
esp_err_t gpio_edge_input_wait(gpio_edge_input_t *input, TickType_t timeout_ticks);

/**
 * Read the current level of the input's pin (0 or 1).
 */
esp_err_t gpio_edge_input_read_level(gpio_edge_input_t *input, bool *level);

/**
 * Opaque handle for a GPIO pulse counter: an ISR increments a counter on
 * each edge (protected by a spinlock), and gpio_pulse_counter_drain() atomically
 * reads and resets it. Use this for simple digital speed/pulse sensors that
 * don't warrant a full PCNT hardware unit (see pcnt_encoder.h for that).
 */
typedef struct {
    gpio_num_t pin;
    volatile uint32_t count;
    portMUX_TYPE spinlock;
} gpio_pulse_counter_t;

/**
 * Configure a GPIO as a pulse-counting digital input.
 *
 * @param counter    handle to initialize (counter->pin must already be set)
 * @param intr_type  typically GPIO_INTR_POSEDGE (count rising edges only)
 */
esp_err_t gpio_pulse_counter_init(gpio_pulse_counter_t *counter, gpio_int_type_t intr_type);

/**
 * Atomically read and reset the pulse count accumulated since the last call.
 */
esp_err_t gpio_pulse_counter_drain(gpio_pulse_counter_t *counter, uint32_t *count);

/**
 * Configure a GPIO as a simple digital output (relays, enable pins...).
 */
esp_err_t gpio_digital_output_init(gpio_num_t pin, bool initial_level);

/**
 * Set the level of a previously-configured digital output.
 */
esp_err_t gpio_digital_output_set(gpio_num_t pin, bool level);

#endif // PERIPHERALS_GPIO_DIGITAL_H_
