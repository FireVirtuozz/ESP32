#include "sensorsLib.h"
#include "driver/gpio.h"
#include "logLib.h"

#include "esp_rom_sys.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char * TAG = "sensors_library";

#if USE_HCSR04

#define TRIG_PIN 27
#define ECHO_PIN 26
#define PULSE_TRIG_DURATION 10 //10us
#define ECHO_TIMEOUT 60 //60ms

static SemaphoreHandle_t sem_hcsr = NULL;

static volatile int64_t echo_duration;

static void IRAM_ATTR echo_isr_handler(void* arg)
{
    BaseType_t taskAwoken = pdFALSE;
    static bool rising = true;
    static int64_t last_timestamp = -1;

    if (rising) {
        last_timestamp = esp_timer_get_time();
    } else { //falling edge
        echo_duration = esp_timer_get_time() - last_timestamp;
        xSemaphoreGiveFromISR(sem_hcsr, &taskAwoken);
        portYIELD_FROM_ISR(taskAwoken); //wake up the task waiting for semaphore (trig_echo)
    }
    rising = !rising;
}

void init_hcsr() {
    esp_err_t err;

    //avoid re-initialization
    if (sem_hcsr != NULL) {
        log_msg(TAG, "HC-SR04 already initialized");
        return;
    }
    sem_hcsr = xSemaphoreCreateBinary();
    if (sem_hcsr == NULL) {
        log_msg(TAG, "Error creating semaphore");
        return;
    }

    err = gpio_reset_pin(TRIG_PIN);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), TRIG_PIN);
        return;
    }
    err = gpio_reset_pin(ECHO_PIN);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), ECHO_PIN);
        return;
    }

    err = gpio_set_direction(TRIG_PIN, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction pin %d", esp_err_to_name(err), TRIG_PIN);
        return;
    }
    err = gpio_set_direction(ECHO_PIN, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction pin %d", esp_err_to_name(err), ECHO_PIN);
        return;
    }

    err = gpio_set_intr_type(ECHO_PIN, GPIO_INTR_ANYEDGE);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting interrupt type pin %d", esp_err_to_name(err), ECHO_PIN);
        return;
    }
    err = gpio_install_isr_service(0);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) installing isr pin %d", esp_err_to_name(err), ECHO_PIN);
        return;
    }
    err = gpio_isr_handler_add(ECHO_PIN, echo_isr_handler, NULL);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding isr handler pin %d", esp_err_to_name(err), ECHO_PIN);
        return;
    }
    //or gpio_isr_register()
    log_msg(TAG, "HC-SR04 initialized");
    
}

int64_t trigger_echo() {
    esp_err_t err;

    if (sem_hcsr == NULL) {
        return -1;
    }

    log_msg(TAG, "Triggering echo..");

    err = gpio_set_level(TRIG_PIN, 1);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting level on pin : %d", esp_err_to_name(err), TRIG_PIN);
        return -1;
    }
    esp_rom_delay_us(PULSE_TRIG_DURATION);
    err = gpio_set_level(TRIG_PIN, 0);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting level on pin : %d", esp_err_to_name(err), TRIG_PIN);
        return -1;
    }

    log_msg(TAG, "Echo triggered");

    if (xSemaphoreTake(sem_hcsr, pdMS_TO_TICKS(ECHO_TIMEOUT)) == pdFALSE) {
        return -1; //after tiemout
    } else {
        return echo_duration; //semaphore given from ISR
    }
}

#endif