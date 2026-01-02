#ifndef NVSLIB_H_
#define NVSLIB_H_

#include <nvs_flash.h>
#include <esp_err.h>
#include <esp_log.h>
#include <nvs.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mqttLib.h"

//Function to init nvs
esp_err_t nvs_init();

//Function to load an nvs int with key and I/O value
esp_err_t load_nvs_int(const char *key, int *val);

//Function to save a value associated with key
esp_err_t save_nvs_int(const char *key, const int value);

//Function to load an nvs string with key and I/O value
esp_err_t load_nvs_str(const char *key, char *val);

//Function to save a string value associated with key
esp_err_t save_nvs_str(const char *key, const char* value);

//Function to load an nvs string with key and I/O value
esp_err_t load_nvs_blob(const char *key, uint8_t *val, size_t length);

//Function to save a string value associated with key
esp_err_t save_nvs_blob(const char *key, uint8_t * value, size_t length);

//Function to list namespace "storage" in nvs (debug)
void list_storage();

//Function to close nvs
void close_nvs();

#endif