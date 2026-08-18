#ifndef KY033_H_
#define KY033_H_

#include <inttypes.h>
#include <esp_err.h>

// KY-033: optical line-tracking/speed sensor, single-channel digital pulse
// output (no direction info). Read via hardware PCNT.
#define KY033_GPIO 8

esp_err_t init_ky033(void);

/** Pulse count over the last 20ms window. */
esp_err_t get_pulses_count_20ms(uint16_t *count);

/** Pulse count over a 100ms sliding window, refreshed every 20ms. */
esp_err_t get_pulses_count_100ms(uint16_t *count);

#endif // KY033_H_
