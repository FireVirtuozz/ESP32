#ifndef KY031_H_
#define KY031_H_

#include <esp_err.h>

// KY-031: knock/vibration sensor, digital output.
#define KY031_GPIO 23

esp_err_t init_ky031(void);

#endif // KY031_H_
