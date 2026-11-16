#ifndef FC33_H_
#define FC33_H_

#include <esp_err.h>

// FC-33: speed/pulse sensor module, digital pulse output.
// Pulses are counted over a 100ms window.
#define FC33_GPIO 23

esp_err_t init_fc33(void);

#endif // FC33_H_
