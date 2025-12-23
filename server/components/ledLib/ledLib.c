#include "ledLib.h"
#include "driver/gpio.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static esp_err_t load_led_state_nvs();
static esp_err_t save_led_state_nvs();

static SemaphoreHandle_t xMutex = NULL;

static const char *TAG = "led_library";

static nvs_handle_t my_handle;

static int led_state;

/**
 * Initalize led
 * -Reset pin
 * -Set output mode
 */
void led_init(void) {

    xMutex = xSemaphoreCreateMutex();

    if( xMutex == NULL )
    {
        return;
    }

    //reset gpio pin
    gpio_reset_pin(LED_PIN);
    //set output mode for gpio
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

    /*
    Do not initialize NVS here if it is already initialized in Wifi.
    Initialize ONCE!
    Or do it in app_main, or a library specific to initialization

    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    */

    // Semaphore cannot be used before a call to xSemaphoreCreateMutex().
    // This is a macro so pass the variable in directly.


    // Open NVS handle
    ESP_LOGI(TAG, "\nOpening Non-Volatile Storage (NVS) handle...");
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return;
    }

    ESP_ERROR_CHECK(load_led_state_nvs());

}

/**
 * Led on : set level of gpio to HIGH
 */
void led_on(void) {

    int need_save = 0;

    if (xMutex == NULL) {
        return;
    }

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        if (led_state == 0) {
            led_state = 1;
            gpio_set_level(LED_PIN, led_state);
            need_save = 1;
        }
        xSemaphoreGive(xMutex);
    }

    if (need_save) {
        save_led_state_nvs();
    }
}

/**
 * Led off : set level of gpio to LOW
 */
void led_off(void) {

    int need_save = 0;

    if (xMutex == NULL) {
        return;
    }

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        if (led_state != 0) {
            led_state = 0;
            gpio_set_level(LED_PIN, led_state);
            need_save = 1;
        }
        xSemaphoreGive(xMutex);
    }

    if (need_save) {
        save_led_state_nvs();
    }
}

/**
 * Led toggle : reverse level
 */
void led_toggle(void) {

    if (xMutex == NULL) {
        return;
    }

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        led_state = !led_state;
        gpio_set_level(LED_PIN, led_state);
        xSemaphoreGive(xMutex);
        save_led_state_nvs();
    }
}

static esp_err_t load_led_state_nvs() {

    int need_init = 0;

    if (xMutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Read back the value
    int32_t read_led = 0;
    ESP_LOGI(TAG, "\nReading counter from NVS...");
    esp_err_t err = nvs_get_i32(my_handle, "led_state", &read_led);

    //wait for 100 ticks
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        switch (err) {
            case ESP_OK:
                ESP_LOGI(TAG, "Read led_state = %" PRIu32, read_led);
                led_state = (int)read_led;
                gpio_set_level(LED_PIN, led_state);
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                led_state = 0;
                gpio_set_level(LED_PIN, 0);
                need_init = 1;
                ESP_LOGW(TAG, "The value is not initialized yet!");
                break;
            default:
                led_state = 0;
                gpio_set_level(LED_PIN, 0);
                ESP_LOGE(TAG, "Error (%s) reading!", esp_err_to_name(err));
        }
        xSemaphoreGive(xMutex);
    } else {
        err = ESP_ERR_TIMEOUT;
    }

    if (need_init) {
        save_led_state_nvs(); // Init key
    }

    return err;
}

static esp_err_t save_led_state_nvs() {

    if (xMutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int32_t ledState = 0;
    if (xSemaphoreTake(xMutex, portMAX_DELAY) != pdTRUE) {
        
        return ESP_ERR_TIMEOUT;
    }
    ledState = (int32_t)led_state;
    xSemaphoreGive(xMutex);

    // Store an integer value
    ESP_LOGI(TAG, "\nWriting counter to NVS...");
    esp_err_t err = nvs_set_i32(my_handle, "led_state", ledState);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write counter!");
    }

    // Commit changes
    // After setting any values, nvs_commit() must be called to ensure changes are written
    // to flash storage. Implementations may write to storage at other times,
    // but this is not guaranteed.
    ESP_LOGI(TAG, "\nCommitting updates in NVS...");
    err = nvs_commit(my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS changes!");
    }
    return err;
}

void close_led() {

    if (xMutex == NULL) {
        return;
    }

    // Close
    nvs_close(my_handle);
    ESP_LOGI(TAG, "NVS handle closed.");

    vSemaphoreDelete(xMutex);
    xMutex = NULL;
}

int get_led_state() {
    int led = -1;

    if (xMutex == NULL) {
        return led;
    }

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        led = led_state;
        xSemaphoreGive(xMutex);
    }
    return led;
}