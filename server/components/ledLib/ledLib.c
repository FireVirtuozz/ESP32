#include "ledLib.h"
#include "driver/gpio.h"
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvsLib.h"
#include "mqttLib.h"
#include <stdarg.h>
#include <esp_err.h>

static SemaphoreHandle_t xMutex = NULL;

static const char *TAG = "led_library";

static int led_state = 0;

/**
 * Initalize led
 * -Initialize mutex
 * -Reset pin
 * -Set output mode
 * -Load previous state in nvs and apply it
 */
void led_init() {

    if( xMutex != NULL )
    {
        log_mqtt(LOG_WARN, TAG, true, "Mutex already initialized");
        return;
    }

    xMutex = xSemaphoreCreateMutex();

    if( xMutex == NULL )
    {
        log_mqtt(LOG_ERROR, TAG, true, "Error on mutex creation");
        return;
    }

    //reset gpio pin
    esp_err_t err = gpio_reset_pin(LED_PIN);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) resetting pin %d", esp_err_to_name(err), LED_PIN);
        return;
    }

    //set output mode for gpio
    err = gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting direction pin %d", esp_err_to_name(err), LED_PIN);
        return;
    }

    //load led state in nvs and apply it to gpio
    err = load_nvs_int("led_state", &led_state);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) loading nvs int : led_state", esp_err_to_name(err));
        return;
    }
    err = gpio_set_level(LED_PIN, led_state);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting level on pin : %d", esp_err_to_name(err), LED_PIN);
        return;
    }

    //ESP_LOGI(TAG, "Led initialized on pin %d", LED_PIN);
    log_mqtt(LOG_INFO, TAG, true, "Led initialized on pin %d", LED_PIN);

}

/**
 * Led on : set level of gpio to HIGH and save it to nvs
 */
void led_on() {

    if (xMutex == NULL) {
        log_mqtt(LOG_ERROR, TAG, true, "Error on mutex creation");
        return;
    }

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        if (led_state == 0) {
            led_state = 1;
            esp_err_t err = gpio_set_level(LED_PIN, led_state);
            if (err != ESP_OK) {
                log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting level on pin : %d", esp_err_to_name(err), LED_PIN);
                xSemaphoreGive(xMutex);
                return;
            }
            err = save_nvs_int("led_state", led_state);
            if (err != ESP_OK) {
                log_mqtt(LOG_ERROR, TAG, true, "Error (%s) loading saving led_state in nvs", esp_err_to_name(err));
                xSemaphoreGive(xMutex);
                return;
            }
            //ESP_LOGI(TAG, "Led on");
            log_mqtt(LOG_INFO, TAG, true, "Led on");
        }
        xSemaphoreGive(xMutex);
    }
}

/**
 * Led off : set level of gpio to LOW and save it to nvs
 */
void led_off() {

    if (xMutex == NULL) {
        log_mqtt(LOG_ERROR, TAG, true, "Error on mutex creation");
        return;
    }

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        if (led_state != 0) {
            led_state = 0;
            esp_err_t err = gpio_set_level(LED_PIN, led_state);
            if (err != ESP_OK) {
                log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting level on pin : %d", esp_err_to_name(err), LED_PIN);
                xSemaphoreGive(xMutex);
                return;
            }
            err = save_nvs_int("led_state", led_state);
            if (err != ESP_OK) {
                log_mqtt(LOG_ERROR, TAG, true, "Error (%s) loading saving led_state in nvs", esp_err_to_name(err));
                xSemaphoreGive(xMutex);
                return;
            }
            log_mqtt(LOG_INFO, TAG, true, "Led off");
        }
        xSemaphoreGive(xMutex);
    }
}

/**
 * Led toggle : reverse level and save it to nvs
 */
void led_toggle() {

    if (xMutex == NULL) {
        log_mqtt(LOG_ERROR, TAG, true, "Error on mutex creation");
        return;
    }

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        led_state = !led_state;
        esp_err_t err = gpio_set_level(LED_PIN, led_state);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting level on pin : %d", esp_err_to_name(err), LED_PIN);
            xSemaphoreGive(xMutex);
            return;
        }
        err = save_nvs_int("led_state", led_state);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) loading saving led_state in nvs", esp_err_to_name(err));
            xSemaphoreGive(xMutex);
            return;
        }
        //ESP_LOGI(TAG, "Led toggled to : %d", led_state);
        log_mqtt(LOG_INFO, TAG, true, "Led toggled to : %d", led_state);
        xSemaphoreGive(xMutex);
    }
}

/**
 * Close led
 * -Destroy mutex
 */
void close_led() {

    if (xMutex == NULL) {
        log_mqtt(LOG_ERROR, TAG, true, "Error on mutex creation");
        return;
    }

    vSemaphoreDelete(xMutex);
    xMutex = NULL;

    //ESP_LOGI(TAG, "Led closed");
    log_mqtt(LOG_INFO, TAG, true, "Led closed");
}

/**
 * Get led state
 * @return int current led state, -1 if error
 */
int get_led_state() {
    int led = -1;

    if (xMutex == NULL) {
        log_mqtt(LOG_ERROR, TAG, true, "Error on mutex creation");
        return led;
    }

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        led = led_state;
        xSemaphoreGive(xMutex);
    }
    return led;
}