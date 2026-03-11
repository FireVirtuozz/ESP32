#ifndef SENSORSLIB_H_
#define SENSORSLIB_H_

#define USE_HCSR04 1

#include <inttypes.h>
#include <esp_err.h>

#if USE_HCSR04

/**
 * Initialize HC-SR04 sensor gpios
 */
void init_hcsr();

/**
 * triggers an echo
 * blocking function for echo's duration
 * @returns duration of echo
 */
int64_t trigger_echo();

#endif

#endif