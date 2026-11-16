#ifndef KY032_H_
#define KY032_H_

#include <esp_err.h>

// KY-032: IR obstacle avoidance sensor, digital output (adjustable threshold pot).
#define KY032_GPIO 23

esp_err_t init_ky032(void);

#endif // KY032_H_
