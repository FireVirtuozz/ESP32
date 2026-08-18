#ifndef DS18B20_H_
#define DS18B20_H_

#include <esp_err.h>

// DS18B20: 1-Wire digital temperature sensor.
// Bit-banged manually (not via RMT) with critical sections around each
// bit's precise timing, matching the original firmware exactly — this
// sensor was entirely missing from the first pass of this refactor.
// Payload: 16-bit big-endian temperature * 100 (e.g. 24.37C -> 2437).
#define DS18B20_GPIO 23 // shared with several other test sensors, see note in sensors_lib.c

esp_err_t init_ds18b20(void);

#endif // DS18B20_H_
