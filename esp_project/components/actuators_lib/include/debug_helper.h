#ifndef DEBUG_HELPER_H_
#define DEBUG_HELPER_H_

#include <esp_err.h>

/**
 * Print LEDC and GPIO diagnostic information to the log, gated by
 * CONFIG_DEBUG_LEDC and CONFIG_DEBUG_GPIO. Each actuator's timer is only
 * inspected if that actuator is itself enabled (CONFIG_USE_xxx), to avoid
 * referencing timer configs that were never compiled in.
 */
esp_err_t print_esp_info_ledc(void);

/**
 * Dump the full GPIO configuration table to stdout, gated by CONFIG_DEBUG_GPIO.
 */
esp_err_t dump_gpio_stats(void);

#endif
