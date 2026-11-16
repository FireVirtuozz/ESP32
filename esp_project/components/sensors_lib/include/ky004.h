#ifndef KY004_H_
#define KY004_H_

#include <esp_err.h>

// KY-004: push button module, digital output.
#define KY004_GPIO 23

esp_err_t init_ky004(void);

#endif // KY004_H_
