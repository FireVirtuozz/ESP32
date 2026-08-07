#include "simple_led.h"
#include "driver/gpio.h"
#include "nvs_lib.h"
#include "log_lib.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <inttypes.h>
#include <esp_err.h>

// GPIO pin the built-in LED is connected to
#define LED_PIN 2

static const char *TAG = "simple_led_library";

static bool led_state = false;
// Mutex protecting concurrent access to led_state / GPIO level
static SemaphoreHandle_t xMutex = NULL;

esp_err_t led_init(void) {
#if CONFIG_USE_BUILTIN_LED
    if (xMutex != NULL) {
        log_msg(TAG, "Mutex already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    xMutex = xSemaphoreCreateMutex();
    if (xMutex == NULL) {
        log_msg(TAG, "Error creating mutex");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = gpio_reset_pin(LED_PIN);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), LED_PIN);
        return err;
    }

    err = gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction on pin %d", esp_err_to_name(err), LED_PIN);
        return err;
    }

#if CONFIG_SAVE_LED
    // Restore last saved state from NVS
    err = load_nvs_int("led_state", (int*)&led_state);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) loading NVS int: led_state", esp_err_to_name(err));
        return err;
    }
#endif

    err = gpio_set_level(LED_PIN, led_state);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting level on pin %d", esp_err_to_name(err), LED_PIN);
        return err;
    }

    log_msg(TAG, "Simple LED initialized on pin %d", LED_PIN);
    return ESP_OK;
#else
    return ESP_OK;
#endif
}

esp_err_t led_on(void) {
#if CONFIG_USE_BUILTIN_LED
    if (xMutex == NULL) {
        log_msg(TAG, "Mutex not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_OK;
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        if (!led_state) {
            led_state = true;
            err = gpio_set_level(LED_PIN, led_state);
            if (err != ESP_OK) {
                log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) setting level on pin %d", esp_err_to_name(err), LED_PIN);
                xSemaphoreGive(xMutex);
                return err;
            }
#if CONFIG_SAVE_LED
            err = save_nvs_int("led_state", led_state);
            if (err != ESP_OK) {
                log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) saving led_state in NVS", esp_err_to_name(err));
                xSemaphoreGive(xMutex);
                return err;
            }
#endif
            log_msg(TAG, "Led on");
        }
        xSemaphoreGive(xMutex);
    }
    return err;
#else
    return ESP_OK;
#endif
}

esp_err_t led_off(void) {
#if CONFIG_USE_BUILTIN_LED
    if (xMutex == NULL) {
        log_msg(TAG, "Mutex not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_OK;
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        if (led_state) {
            led_state = false;
            err = gpio_set_level(LED_PIN, led_state);
            if (err != ESP_OK) {
                log_msg(TAG, "Error (%s) setting level on pin %d", esp_err_to_name(err), LED_PIN);
                xSemaphoreGive(xMutex);
                return err;
            }
#if CONFIG_SAVE_LED
            err = save_nvs_int("led_state", led_state);
            if (err != ESP_OK) {
                log_msg(TAG, "Error (%s) saving led_state in NVS", esp_err_to_name(err));
                xSemaphoreGive(xMutex);
                return err;
            }
#endif
            log_msg(TAG, "Led off");
        }
        xSemaphoreGive(xMutex);
    }
    return err;
#else
    return ESP_OK;
#endif
}

esp_err_t led_toggle(void) {
#if CONFIG_USE_BUILTIN_LED
    if (xMutex == NULL) {
        log_msg(TAG, "Mutex not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_OK;
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        led_state = !led_state;
        err = gpio_set_level(LED_PIN, led_state);
        if (err != ESP_OK) {
            log_msg(TAG, "Error (%s) setting level on pin %d", esp_err_to_name(err), LED_PIN);
            xSemaphoreGive(xMutex);
            return err;
        }
#if CONFIG_SAVE_LED
        err = save_nvs_int("led_state", led_state);
        if (err != ESP_OK) {
            log_msg(TAG, "Error (%s) saving led_state in NVS", esp_err_to_name(err));
            xSemaphoreGive(xMutex);
            return err;
        }
#endif
        log_msg(TAG, "Led toggled to: %d", led_state);
        xSemaphoreGive(xMutex);
    }
    return err;
#else
    return ESP_OK;
#endif
}

esp_err_t get_led_state(int *state) {
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_USE_BUILTIN_LED
    if (xMutex == NULL) {
        log_msg(TAG, "Mutex not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        *state = led_state;
        xSemaphoreGive(xMutex);
    }
    return ESP_OK;
#else
    *state = -1;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t close_simple_led(void) {
    if (xMutex == NULL) {
        // Never initialized, nothing to release: not an error.
        return ESP_OK;
    }

    vSemaphoreDelete(xMutex);
    xMutex = NULL;
    return ESP_OK;
}
