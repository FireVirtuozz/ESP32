#include "peripherals/gpio_digital.h"
#include "log_lib.h"

static const char *TAG = "gpio_digital_peripheral";

// --- Shared ISR service bookkeeping ---
// gpio_install_isr_service() is process-wide and can only succeed once;
// every sensor calling this helper attempts it and treats "already installed"
// as success, so callers don't need to coordinate installation order.
static esp_err_t ensure_isr_service_installed(void) {
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        log_msg(TAG, "Error (%s) installing GPIO ISR service", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

// --- Edge state input ---

static void IRAM_ATTR edge_input_isr_handler(void *arg) {
    gpio_edge_input_t *input = (gpio_edge_input_t *)arg;
    BaseType_t task_awoken = pdFALSE;
    xSemaphoreGiveFromISR(input->sem, &task_awoken);
    portYIELD_FROM_ISR(task_awoken);
}

esp_err_t gpio_edge_input_init(gpio_edge_input_t *input, gpio_int_type_t intr_type) {
    if (input == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (input->sem != NULL) {
        log_msg(TAG, "GPIO %d edge input already initialized", input->pin);
        return ESP_ERR_INVALID_STATE;
    }

    input->sem = xSemaphoreCreateBinary();
    if (input->sem == NULL) {
        log_msg(TAG, "Error creating semaphore for GPIO %d", input->pin);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = gpio_reset_pin(input->pin);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), input->pin);
        return err;
    }

    err = gpio_set_direction(input->pin, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction on pin %d", esp_err_to_name(err), input->pin);
        return err;
    }

    err = gpio_set_intr_type(input->pin, intr_type);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting interrupt type on pin %d", esp_err_to_name(err), input->pin);
        return err;
    }

    err = ensure_isr_service_installed();
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_isr_handler_add(input->pin, edge_input_isr_handler, input);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding ISR handler on pin %d", esp_err_to_name(err), input->pin);
        return err;
    }

    log_msg(TAG, "Edge input initialized on GPIO %d", input->pin);
    return ESP_OK;
}

esp_err_t gpio_edge_input_wait(gpio_edge_input_t *input, TickType_t timeout_ticks) {
    if (input == NULL || input->sem == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return (xSemaphoreTake(input->sem, timeout_ticks) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t gpio_edge_input_read_level(gpio_edge_input_t *input, bool *level) {
    if (input == NULL || level == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *level = gpio_get_level(input->pin);
    return ESP_OK;
}

// --- Pulse counter ---

static void IRAM_ATTR pulse_counter_isr_handler(void *arg) {
    gpio_pulse_counter_t *counter = (gpio_pulse_counter_t *)arg;
    taskENTER_CRITICAL_ISR(&counter->spinlock);
    counter->count++;
    taskEXIT_CRITICAL_ISR(&counter->spinlock);
}

esp_err_t gpio_pulse_counter_init(gpio_pulse_counter_t *counter, gpio_int_type_t intr_type) {
    if (counter == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    counter->count = 0;
    counter->spinlock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;

    esp_err_t err = gpio_reset_pin(counter->pin);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), counter->pin);
        return err;
    }

    err = gpio_set_direction(counter->pin, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction on pin %d", esp_err_to_name(err), counter->pin);
        return err;
    }

    err = gpio_set_intr_type(counter->pin, intr_type);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting interrupt type on pin %d", esp_err_to_name(err), counter->pin);
        return err;
    }

    err = ensure_isr_service_installed();
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_isr_handler_add(counter->pin, pulse_counter_isr_handler, counter);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding ISR handler on pin %d", esp_err_to_name(err), counter->pin);
        return err;
    }

    log_msg(TAG, "Pulse counter initialized on GPIO %d", counter->pin);
    return ESP_OK;
}

esp_err_t gpio_pulse_counter_drain(gpio_pulse_counter_t *counter, uint32_t *count) {
    if (counter == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    taskENTER_CRITICAL(&counter->spinlock);
    *count = counter->count;
    counter->count = 0;
    taskEXIT_CRITICAL(&counter->spinlock);

    return ESP_OK;
}

// --- Digital output ---

esp_err_t gpio_digital_output_init(gpio_num_t pin, bool initial_level) {
    esp_err_t err = gpio_reset_pin(pin);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), pin);
        return err;
    }

    err = gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction on pin %d", esp_err_to_name(err), pin);
        return err;
    }

    return gpio_digital_output_set(pin, initial_level);
}

esp_err_t gpio_digital_output_set(gpio_num_t pin, bool level) {
    return gpio_set_level(pin, level);
}
