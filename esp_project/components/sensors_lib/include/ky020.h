#ifndef KY020_H_
#define KY020_H_

#include <esp_err.h>

// KY-020: tilt (ball) switch, digital output.
#define KY020_GPIO 23

esp_err_t init_ky020(void);

#endif // KY020_H_
