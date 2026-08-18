#ifndef KY040_H_
#define KY040_H_

#include <esp_err.h>

// KY-040: rotary encoder with push button.
// CLK/DT (rotation) are decoded in hardware via PCNT quadrature — an
// improvement over the original's software GPIO-interrupt quadrature
// decoding (same idea already used for KY033, now genuinely applicable
// here since KY040 has real A/B channels). SW (button) stays on a simple
// GPIO edge input, which is the right tool for a single digital button.
#define KY040_CLK_GPIO 23
#define KY040_DT_GPIO  21
#define KY040_SW_GPIO  22

esp_err_t init_ky040(void);

#endif // KY040_H_
