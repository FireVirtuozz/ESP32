#ifndef KY017_H_
#define KY017_H_

#include <esp_err.h>

// KY-017: mercury tilt switch, digital output.
#define KY017_GPIO 23

esp_err_t init_ky017(void);

#endif // KY017_H_
