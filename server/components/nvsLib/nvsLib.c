#include "nvsLib.h"
#include "mqttLib.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <esp_log.h>
#include <esp_err.h>

//Mutex to access nvs
static SemaphoreHandle_t xMutex = NULL;

//Tag for logs
static const char *TAG = "nvs_library";

//handle to access nvs
static nvs_handle_t my_handle;

//struct to convert types into string to print them
typedef struct {
    nvs_type_t type;
    const char *str;
} type_str_pair_t;

//different kinds of types
static const type_str_pair_t type_str_pair[] = {
    { NVS_TYPE_I8, "i8" },
    { NVS_TYPE_U8, "u8" },
    { NVS_TYPE_U16, "u16" },
    { NVS_TYPE_I16, "i16" },
    { NVS_TYPE_U32, "u32" },
    { NVS_TYPE_I32, "i32" },
    { NVS_TYPE_U64, "u64" },
    { NVS_TYPE_I64, "i64" },
    { NVS_TYPE_STR, "str" },
    { NVS_TYPE_BLOB, "blob" },
    { NVS_TYPE_ANY, "any" },
};

//number of pairs
static const size_t TYPE_STR_PAIR_SIZE = sizeof(type_str_pair) / sizeof(type_str_pair[0]);

/**
 * Function transforming NVS type to str
 * @param type nvs type
 */
static const char *type_to_str(nvs_type_t type) {
    //go through each pair and return the right string
    for (int i = 0; i < TYPE_STR_PAIR_SIZE; i++) {
        const type_str_pair_t *p = &type_str_pair[i];
        if (p->type == type) {
            return  p->str;
        }
    }

    return "Unknown";
}

/**
 * Initalize NVS
 * -Create Mutex (do it once)
 * -NVS flash init
 * -Open NVS handle
 */
esp_err_t nvs_init() {

    //if mutex already initialized, quit
    if( xMutex != NULL )
    {
        //log_mqtt()
        log_mqtt(LOG_WARN, TAG, true, "NVS already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    xMutex = xSemaphoreCreateMutex();

    //If error in Create mutex
    if( xMutex == NULL )
    {
        log_mqtt(LOG_ERROR, TAG, true, "Error in mutex creation");
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        esp_err_t err = nvs_flash_erase();
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) erase nvs flash", esp_err_to_name(err));
            return err;
        }
        err = nvs_flash_init();
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) initiating flash after erase", esp_err_to_name(err));
            return err;
        }
    }
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) initiating flash", esp_err_to_name(err));
        return err;
    }

    // Open NVS handle
    log_mqtt(LOG_INFO, TAG, true, "Opening Non-Volatile Storage (NVS) handle...");
    err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) opening NVS handle!", esp_err_to_name(err));
    }
    return err;
}

/**
 * Function to load an int in nvs
 * -Take mutex, read nvs and handle return value, init if needed
 * @param key str key
 * @param val int in/out value updated*
 */
esp_err_t load_nvs_int(const char *key, int *val) {

    int need_init = 0;

    //if mutex destroyed or not initialized
    if (xMutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    int32_t read_val = 0;
    esp_err_t err = ESP_ERR_TIMEOUT;

    //wait to take mutex
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {

        log_mqtt(LOG_INFO, TAG, false, "Reading %s from NVS...", key);
        //read value i32
        err = nvs_get_i32(my_handle, key, &read_val);

        //switch on return read value
        switch (err) {
            // ok : update value
            case ESP_OK:
                log_mqtt(LOG_INFO, TAG, true, "Read %s = %" PRIu32, key, read_val);
                *val = (int)read_val;
                break;
            // not found : initialize value to 0 (default)
            case ESP_ERR_NVS_NOT_FOUND:
                need_init = 1;
                log_mqtt(LOG_WARN, TAG, true, "The value is not initialized yet!");
                break;
            //other : error
            default:
                log_mqtt(LOG_ERROR, TAG, true, "Error (%s) reading!", esp_err_to_name(err));
        }
        //free mutex
        xSemaphoreGive(xMutex);
    }

    //if init is needed (here because avoids deadlock mutex)
    if (need_init) {
        *val = 0; //update value to 0 (default)
        return save_nvs_int(key, 0); // Init key to 0 (default)
    }

    return err;
}

/**
 * Function to load a string in nvs
 * -Take mutex, read nvs and handle return value, init if needed
 * @param key str key
 * @param val int in/out value updated*
 * @param length length of string (0 auto calculated)
 */
esp_err_t load_nvs_blob(const char *key, uint8_t *val, size_t length) {

    int need_init = 0;

    //if mutex destroyed or not initialized
    if (xMutex == NULL) {
        log_mqtt(LOG_ERROR, TAG, true, "Mutex not initalized!");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_ERR_TIMEOUT;

    //wait to take mutex
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {

        size_t required_size = 0;

        log_mqtt(LOG_INFO, TAG, false, "Reading blob %s from NVS...", key);
        err = nvs_get_blob(my_handle, key, NULL, &required_size);
        if (err == ESP_OK) {
            uint8_t* tmp = malloc(required_size);
            if (!tmp) {
                log_mqtt(LOG_ERROR, TAG, true, "Memory allocation failed for blob %s", key);
                xSemaphoreGive(xMutex);
                return ESP_ERR_NO_MEM;
            }
            err = nvs_get_blob(my_handle, key, tmp, &required_size);
            if (err == ESP_OK) {
                memcpy(val, tmp, required_size < length ? required_size : length);
            }
            free(tmp);
        } else if (err == ESP_ERR_NVS_NOT_FOUND) {
            need_init = 1;
            log_mqtt(LOG_WARN, TAG, true, "The blob is not initialized yet!");
        } else {
            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) reading!", esp_err_to_name(err));
        }
        //free mutex
        xSemaphoreGive(xMutex);
    }

    //if init is needed (here because avoids deadlock mutex)
    if (need_init) {
        return save_nvs_blob(key, val, length);
    }

    return err;
}

/**
 * Function to save a string in nvs
 * -Take mutex, set nvs value and commit changes to handle
 * @param key key string of value
 * @param value const str value to save
 */
esp_err_t save_nvs_blob(const char *key, uint8_t* value, size_t length) {

    if (xMutex == NULL) {
        log_mqtt(LOG_ERROR, TAG, true, "Mutex not initalized!");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        
        log_mqtt(LOG_INFO, TAG, true, "Writing blob %s to NVS...", key);
        // Store blob
        esp_err_t err = nvs_set_blob(my_handle, key, value, length);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Failed to write blob %s!", key);
            xSemaphoreGive(xMutex);
            return err;
        }

        // Commit changes
        // After setting any values, nvs_commit() must be called to ensure changes are written
        // to flash storage. Implementations may write to storage at other times,
        // but this is not guaranteed.
        log_mqtt(LOG_INFO, TAG, true, "Committing updates in NVS...");
        err = nvs_commit(my_handle);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Failed to commit NVS changes!");
        }
        xSemaphoreGive(xMutex);
        return err;
    }
    return ESP_ERR_INVALID_STATE;
}

/**
 * Function to load a string in nvs
 * -Take mutex, read nvs and handle return value, init if needed
 * @param key str key
 * @param val int in/out value updated*
 * @param length length of string (0 auto calculated)
 */
esp_err_t load_nvs_str(const char *key, char *val) {

    int need_init = 0;

    //if mutex destroyed or not initialized
    if (xMutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_ERR_TIMEOUT;

    //wait to take mutex
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {

        size_t required_size = 0;

        log_mqtt(LOG_INFO, TAG, false, "Reading %s from NVS...", key);
        err = nvs_get_str(my_handle, key, NULL, &required_size);
        if (err == ESP_OK) {
            char* tmp = malloc(required_size);
            if (!tmp) {
                xSemaphoreGive(xMutex);
                return ESP_ERR_NO_MEM;
            }
            err = nvs_get_str(my_handle, key, tmp, &required_size);
            if (err == ESP_OK) {
                strncpy(val, tmp, required_size);
                val[required_size-1] = '\0';
            }
            free(tmp);
        }

        //switch on return read value
        switch (err) {
            // ok : update value
            case ESP_OK:
                log_mqtt(LOG_INFO, TAG, true, "Read %s = %s", key, val);
                break;
            // not found : initialize value to 0 (default)
            case ESP_ERR_NVS_NOT_FOUND:
                need_init = 1;
                log_mqtt(LOG_WARN, TAG, true, "The value is not initialized yet!");
                break;
            //other : error
            default:
                log_mqtt(LOG_ERROR, TAG, true, "Error (%s) reading!", esp_err_to_name(err));
        }
        //free mutex
        xSemaphoreGive(xMutex);
    }

    //if init is needed (here because avoids deadlock mutex)
    if (need_init) {
        val[0] = '\0';
        return save_nvs_str(key, val); // Init key to 0 (default)
    }

    return err;
}

/**
 * Function to save a string in nvs
 * -Take mutex, set nvs value and commit changes to handle
 * @param key key string of value
 * @param value const str value to save
 */
esp_err_t save_nvs_str(const char *key, const char* value) {

    if (xMutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        
        log_mqtt(LOG_INFO, TAG, true, "Writing %s to NVS...", key);
        // Store an integer value
        esp_err_t err = nvs_set_str(my_handle, key, value);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Failed to write %s!", key);
        }

        // Commit changes
        // After setting any values, nvs_commit() must be called to ensure changes are written
        // to flash storage. Implementations may write to storage at other times,
        // but this is not guaranteed.
        log_mqtt(LOG_INFO, TAG, true, "Committing updates in NVS...");
        err = nvs_commit(my_handle);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Failed to commit NVS changes!");
        }
        xSemaphoreGive(xMutex);
        return err;
    }
    return ESP_ERR_INVALID_STATE;
}

/**
 * Function to save an int in nvs
 * -Take mutex, set nvs value and commit changes to handle
 * @param key key string of value
 * @param value const int value to save
 */
esp_err_t save_nvs_int(const char *key, const int value) {

    if (xMutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int32_t val = (int32_t)value;

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        
        log_mqtt(LOG_INFO, TAG, true, "Writing %s to NVS...", key);
        // Store an integer value
        esp_err_t err = nvs_set_i32(my_handle, key, val);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Failed to write %s!", key);
        }

        // Commit changes
        // After setting any values, nvs_commit() must be called to ensure changes are written
        // to flash storage. Implementations may write to storage at other times,
        // but this is not guaranteed.
        log_mqtt(LOG_INFO, TAG, true, "Committing updates in NVS...");
        err = nvs_commit(my_handle);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Failed to commit NVS changes!");
        }
        xSemaphoreGive(xMutex);
        return err;
    }
    return ESP_ERR_INVALID_STATE;
}

/**
 * Function to close nvs
 * -Close nvs handle, delete mutex
 */
void close_nvs() {

    if (xMutex == NULL) {
        return;
    }

    // Close
    nvs_close(my_handle);
    log_mqtt(LOG_INFO, TAG, true, "NVS handle closed.");

    vSemaphoreDelete(xMutex);
    xMutex = NULL;
}

/**
 * Log function to list keys and values in namespace "storage"
 * -Take mutex
 * -Find keys, declare an iterator
 * -Getting info of iterator, handle type, log it and go for the next one
 * -Release iterator, mutex
 */
void list_storage() {

    if (xMutex == NULL) {
        return;
    }

    //take mutex
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {

        log_mqtt(LOG_INFO, TAG, true, "Finding keys in NVS...");
        nvs_iterator_t it = NULL;
        // Find keys in NVS and put it in the iterator
        esp_err_t res = nvs_entry_find("nvs", "storage", NVS_TYPE_ANY, &it);

        while(res == ESP_OK) {

            //getting info of entry : type, key..
            nvs_entry_info_t info;
            nvs_entry_info(it, &info);

            //transform type to str to print it
            const char *type_str =  type_to_str(info.type);

            //if info is i32
            if (info.type == NVS_TYPE_I32) {

                //load value : load function not called to prevent deadlock mutex
                int val;
                int32_t read_val;
                log_mqtt(LOG_INFO, TAG, false, "Reading %s from NVS...", info.key);
                esp_err_t err = nvs_get_i32(my_handle, info.key, &read_val);

                switch (err) {
                    case ESP_OK:
                        val = (int)read_val;
                        log_mqtt(LOG_INFO, TAG, true, "Key: '%s', Type: %s, Value: %d",
                            info.key, type_str, val);
                        break;
                    case ESP_ERR_NVS_NOT_FOUND:
                        log_mqtt(LOG_WARN, TAG, true, "The value is not initialized yet!");
                        break;
                    default:
                        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) reading!", esp_err_to_name(err));
                }
                
            } else {
                log_mqtt(LOG_INFO, TAG, true, "Key: '%s', Type: %s", info.key, type_str);
            }
            //next iterator
            res = nvs_entry_next(&it);
        }
        //release iterator and mutex
        nvs_release_iterator(it);
        xSemaphoreGive(xMutex);
    }
}