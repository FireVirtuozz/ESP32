#ifndef NVSLIB_H_
#define NVSLIB_H_

#include <esp_err.h>
#include <inttypes.h>

/**
 * Initialize nvs
 */
esp_err_t nvs_init();

/**
 * Load an int in NVS
 * @details Using i32
 * @param key key string nvs
 * @param val int pointer to update value
 */
esp_err_t load_nvs_int(const char *key, int *val);

/**
 * Save an int in NVS
 * @details Using i32
 * @param key key string nvs
 * @param val int value to put
 */
esp_err_t save_nvs_int(const char *key, const int value);

/**
 * Load string in NVS
 * @param key key string nvs
 * @param val string to update value
 */
esp_err_t load_nvs_str(const char *key, char *val);

/**
 * Save a str in NVS
 * @param key key string nvs
 * @param val string value to put
 */
esp_err_t save_nvs_str(const char *key, const char* value);

/**
 * Save a blob in NVS
 * @param key key string nvs
 * @param val blob pointer to update
 * @param length length of blob (useless here, keep 0)
 */
esp_err_t load_nvs_blob(const char *key, uint8_t *val, size_t length);

/**
 * Save a blob in NVS
 * @details For u8 arrays
 * @param key key string nvs
 * @param val blob pointer to put
 * @param length length of blob to put
 */
esp_err_t save_nvs_blob(const char *key, uint8_t * value, size_t length);

/**
 * Print a nvs namespace's list
 * @details require debug
 */
void list_storage();

/**
 * Close nvs
 */
void close_nvs();

/**
 * Print all nvs statistics
 * @details require debug
 */
void show_nvs_stats();

#endif