#ifndef BZ181_H_
#define BZ181_H_
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define GPS_RX_GPIO 38

esp_err_t init_gps(void);

#endif //BZ181_H_