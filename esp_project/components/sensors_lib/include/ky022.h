#ifndef KY022_H_
#define KY022_H_

#include <esp_err.h>

// KY-022: infrared receiver. Captures NEC-style IR codes via RMT RX.
#define KY022_GPIO 22

esp_err_t init_ky022(void);

#endif // KY022_H_
