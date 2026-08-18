#ifndef RFID_RC522_H_
#define RFID_RC522_H_

#include <esp_err.h>

// RC522: 13.56MHz RFID reader/writer over SPI.
#define RC522_PIN_MISO 19
#define RC522_PIN_MOSI 23
#define RC522_PIN_CLK  18
#define RC522_PIN_CS   21
#define RC522_PIN_RST  22

esp_err_t init_rfid_rc522(void);

#endif // RFID_RC522_H_
