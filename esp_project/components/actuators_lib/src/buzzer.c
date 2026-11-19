#include "buzzer.h"
#include "actuators_lib.h"
#include "driver/ledc.h"
#include <inttypes.h>
#include <esp_err.h>
#include <stdbool.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "log_lib.h"

static const char *TAG = "buzzer_library";

#if CONFIG_USE_KY006

typedef struct {
    uint16_t frequency_hz;
    uint16_t duration_ms;
    uint8_t volume_percent;
} buzzer_step_t;

static QueueHandle_t buzzer_queue = NULL;
static TaskHandle_t buzzer_task_handle = NULL;

static const buzzer_step_t READY_PATTERN[] = {
    { 784,  80, 35 }, { 0, 40, 0 }, { 988,  80, 35 }, { 0, 40, 0 }, { 1319, 130, 40 },
};
static const buzzer_step_t FRONT_PATTERN[] = {
    { 660, 100, 35 }, { 0, 55, 0 }, { 880, 130, 40 },
};
static const buzzer_step_t REAR_PATTERN[] = {
    { 880, 100, 35 }, { 0, 55, 0 }, { 660, 130, 40 },
};
static const buzzer_step_t EMERGENCY_PATTERN[] = {
    { 1760, 75, 55 }, { 0, 45, 0 }, { 1760, 75, 55 }, { 0, 45, 0 }, { 1760, 120, 55 },
};
static const buzzer_step_t HORN_PATTERN[] = {
    { 523, 120, 45 }, { 0, 35, 0 }, { 659, 120, 45 }, { 0, 35, 0 }, { 784, 190, 50 },
};

ledc_timer_config_t ledc_timer_buzzer = {
    .duty_resolution = BUZZER_RESOLUTION,
    .freq_hz = BUZZER_START_FREQ,
    .speed_mode = BUZZER_SPEED_MODE,
    .timer_num = BUZZER_LEDC_TIMER,
    .clk_cfg = LEDC_AUTO_CLK,
};

static ledc_channel_config_t ledc_channel_buzzer = {
    .channel = BUZZER_LEDC_CHANNEL,
    .duty = 0,
    .gpio_num = BUZZER_GPIO,
    .speed_mode = BUZZER_SPEED_MODE,
    .hpoint = 0,
    .timer_sel = BUZZER_LEDC_TIMER,
    .flags.output_invert = 0,
};

static esp_err_t set_buzzer_tone(uint16_t frequency_hz, uint8_t volume_percent) {
    if (volume_percent == 0 || frequency_hz == 0) {
        return ledc_apply_duty(BUZZER_SPEED_MODE, BUZZER_LEDC_CHANNEL, 0);
    }

    esp_err_t err = ledc_set_freq(BUZZER_SPEED_MODE, BUZZER_LEDC_TIMER, frequency_hz);
    if (err != ESP_OK) {
        return err;
    }
    return ledc_buzzer(volume_percent);
}

static const buzzer_step_t *pattern_steps(buzzer_pattern_t pattern, size_t *count) {
    switch (pattern) {
        case BUZZER_PATTERN_READY:
            *count = sizeof(READY_PATTERN) / sizeof(READY_PATTERN[0]);
            return READY_PATTERN;
        case BUZZER_PATTERN_OBSTACLE_FRONT:
            *count = sizeof(FRONT_PATTERN) / sizeof(FRONT_PATTERN[0]);
            return FRONT_PATTERN;
        case BUZZER_PATTERN_OBSTACLE_REAR:
            *count = sizeof(REAR_PATTERN) / sizeof(REAR_PATTERN[0]);
            return REAR_PATTERN;
        case BUZZER_PATTERN_EMERGENCY:
            *count = sizeof(EMERGENCY_PATTERN) / sizeof(EMERGENCY_PATTERN[0]);
            return EMERGENCY_PATTERN;
        case BUZZER_PATTERN_HORN:
            *count = sizeof(HORN_PATTERN) / sizeof(HORN_PATTERN[0]);
            return HORN_PATTERN;
        default:
            *count = 0;
            return NULL;
    }
}

static void buzzer_sequence_task(void *arg) {
    (void)arg;
    buzzer_pattern_t requested;

    for (;;) {
        xQueueReceive(buzzer_queue, &requested, portMAX_DELAY);

        for (;;) {
            size_t count = 0;
            const buzzer_step_t *steps = pattern_steps(requested, &count);
            bool interrupted = false;

            for (size_t i = 0; i < count; ++i) {
                set_buzzer_tone(steps[i].frequency_hz, steps[i].volume_percent);
                if (xQueueReceive(buzzer_queue, &requested,
                        pdMS_TO_TICKS(steps[i].duration_ms)) == pdTRUE) {
                    interrupted = true;
                    break;
                }
            }

            if (!interrupted) {
                set_buzzer_tone(0, 0);
                break;
            }
        }
    }
}

esp_err_t init_buzzer(void) {
    esp_err_t err;

    err = ledc_timer_config(&ledc_timer_buzzer);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) configuring buzzer timer", esp_err_to_name(err));
        return err;
    }

    err = ledc_channel_config(&ledc_channel_buzzer);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) configuring buzzer channel", esp_err_to_name(err));
        return err;
    }

    buzzer_queue = xQueueCreate(1, sizeof(buzzer_pattern_t));
    if (buzzer_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(buzzer_sequence_task, "buzzer", 2048, NULL, 4,
            &buzzer_task_handle) != pdPASS) {
        vQueueDelete(buzzer_queue);
        buzzer_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    log_msg(TAG, "Buzzer (KY006) initialized");
    return buzzer_play(BUZZER_PATTERN_READY);
}

esp_err_t close_buzzer(void) {
    esp_err_t err;
    esp_err_t first_error = ESP_OK;

    if (buzzer_task_handle != NULL) {
        vTaskDelete(buzzer_task_handle);
        buzzer_task_handle = NULL;
    }
    if (buzzer_queue != NULL) {
        vQueueDelete(buzzer_queue);
        buzzer_queue = NULL;
    }

    err = ledc_timer_pause(ledc_timer_buzzer.speed_mode, ledc_timer_buzzer.timer_num);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) pausing buzzer timer", esp_err_to_name(err));
        first_error = err;
    }

    err = ledc_timer_rst(ledc_timer_buzzer.speed_mode, ledc_timer_buzzer.timer_num);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting buzzer timer", esp_err_to_name(err));
        if (first_error == ESP_OK) first_error = err;
    }

    return first_error;
}

esp_err_t ledc_buzzer(int16_t buzzer_percent) {
    if (buzzer_percent < 0) buzzer_percent = 0;
    if (buzzer_percent > 100) buzzer_percent = 100;

    uint32_t duty = BUZZER_MIN_DUTY + ((BUZZER_MAX_DUTY - BUZZER_MIN_DUTY) * buzzer_percent) / 100;
    esp_err_t err = ledc_apply_duty(BUZZER_SPEED_MODE, BUZZER_LEDC_CHANNEL, duty);
    if (err != ESP_OK) {
        return err;
    }

    log_msg_lvl(ESP_LOG_DEBUG, TAG, "Applied duty %" PRIu32, duty);
    return ESP_OK;
}

esp_err_t buzzer_play(buzzer_pattern_t pattern) {
    if (buzzer_queue == NULL || pattern >= BUZZER_PATTERN_STOP) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueOverwrite(buzzer_queue, &pattern) == pdPASS ? ESP_OK : ESP_FAIL;
}

esp_err_t buzzer_stop(void) {
    if (buzzer_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const buzzer_pattern_t stop = BUZZER_PATTERN_STOP;
    return xQueueOverwrite(buzzer_queue, &stop) == pdPASS ? ESP_OK : ESP_FAIL;
}

#else // !CONFIG_USE_KY006

esp_err_t init_buzzer(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t close_buzzer(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t ledc_buzzer(int16_t buzzer_percent) { (void)buzzer_percent; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t buzzer_play(buzzer_pattern_t pattern) { (void)pattern; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t buzzer_stop(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_KY006
