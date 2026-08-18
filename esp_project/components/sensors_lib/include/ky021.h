#ifndef KY021_H_
#define KY021_H_

#include <esp_err.h>

// KY-021: mini magnetic reed switch, digital output.
#define KY021_GPIO 23

esp_err_t init_ky021(void);

#endif // KY021_H_
