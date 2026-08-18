#ifndef KY003_H_
#define KY003_H_

#include <esp_err.h>

// KY-003: Hall effect (magnetic) sensor, digital pulse output.
// Used here as a speed/RPM sensor: pulses are counted over a 100ms window.
#define KY003_GPIO 23

esp_err_t init_ky003(void);

#endif // KY003_H_
