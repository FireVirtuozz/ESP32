#ifndef RCWL_0515_H_
#define RCWL_0515_H_

#include <esp_err.h>

// RCWL-0515: microwave motion detector, digital output.
#define RCWL_0515_GPIO 35

esp_err_t init_rcwl_0515(void);

#endif // RCWL_0515_H_
