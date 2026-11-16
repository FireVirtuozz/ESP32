#ifndef HCSR04_H_
#define HCSR04_H_

#include <inttypes.h>
#include <stdbool.h>
#include <esp_err.h>

// HC-SR04: ultrasonic distance sensor. Two units used: front (id 0) and
// rear (id 1), each driving the H-Bridge's obstacle-blocking logic.

typedef struct {
    uint8_t hc_id;      // 0 = front, 1 = rear
    int trig_pin;
    int echo_pin;
} hcsr04_config_t;

/**
 * Start both front and rear HC-SR04 monitoring tasks, with a small delay
 * between the two (matches the original firmware's stagger) so their
 * ultrasonic triggers don't fire close enough together to cross-interfere.
 */
esp_err_t init_hcsr04(void);

/**
 * Whether the front sensor currently reports an obstacle close enough to
 * block forward motion (consumed by actuators_lib's h_bridge.c).
 */
esp_err_t get_front_blocked(bool *blocked);

/**
 * Whether the rear sensor currently reports an obstacle close enough to
 * block backward motion (consumed by actuators_lib's h_bridge.c).
 */
esp_err_t get_rear_blocked(bool *blocked);

#endif // HCSR04_H_