#ifndef LIDAR_H_
#define LIDAR_H_
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define LIDAR_RX_GPIO 12

esp_err_t init_lidar(void);

#endif //LIDAR_H_