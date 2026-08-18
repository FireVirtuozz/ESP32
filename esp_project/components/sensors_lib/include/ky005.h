#ifndef KY005_H_
#define KY005_H_

#include <inttypes.h>
#include <esp_err.h>

// KY-005: infrared LED emitter. Transmits NEC-style IR codes via RMT with a
// 38kHz carrier (the standard consumer-IR carrier frequency).
#define KY005_GPIO 23

esp_err_t init_ky005(void);

/**
 * Transmit a 32-bit NEC-style IR code.
 */
esp_err_t ky005_send_code(uint32_t code);

#endif // KY005_H_
