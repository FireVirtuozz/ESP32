#include "ledLib.h"
#include "driver/gpio.h"
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvsLib.h"

static SemaphoreHandle_t xMutex = NULL;

static const char *TAG = "led_library";

static int led_state;

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
        return;
    }

    xMutex = xSemaphoreCreateMutex();

    if( xMutex == NULL )
    {
        return;
    }

    //reset gpio pin
    gpio_reset_pin(LED_PIN);
    //set output mode for gpio
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

    //load led state in nvs and apply it to gpio
    ESP_ERROR_CHECK(load_nvs_int("led_state", &led_state));
    gpio_set_level(LED_PIN, led_state);

    ESP_LOGI(TAG, "Led initialized on pin %d", LED_PIN);

}

/**
 * Led on : set level of gpio to HIGH and save it to nvs
 */
void led_on() {

    if (xMutex == NULL) {
        return;
    }

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        if (led_state == 0) {
            led_state = 1;
            gpio_set_level(LED_PIN, led_state);
            save_nvs_int("led_state", led_state);
            ESP_LOGI(TAG, "Led on");
        }
        xSemaphoreGive(xMutex);
    }
}

/**
 * Led off : set level of gpio to LOW and save it to nvs
 */
void led_off() {

    if (xMutex == NULL) {
        return;
    }

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        if (led_state != 0) {
            led_state = 0;
            gpio_set_level(LED_PIN, led_state);
            save_nvs_int("led_state", led_state);
            ESP_LOGI(TAG, "Led off");
        }
        xSemaphoreGive(xMutex);
    }
}

/**
 * Led toggle : reverse level and save it to nvs
 */
void led_toggle() {

    if (xMutex == NULL) {
        return;
    }

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        led_state = !led_state;
        gpio_set_level(LED_PIN, led_state);
        save_nvs_int("led_state", led_state);
        ESP_LOGI(TAG, "Led toggled to : %d", led_state);
        xSemaphoreGive(xMutex);
    }
}

/**
 * Close led
 * -Destroy mutex
 */
void close_led() {

    if (xMutex == NULL) {
        return;
    }

    vSemaphoreDelete(xMutex);
    xMutex = NULL;

    ESP_LOGI(TAG, "Led closed");
}

/**
 * Get led state
 * @return int current led state, -1 if error
 */
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