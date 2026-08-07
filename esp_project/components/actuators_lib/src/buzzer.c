#include "buzzer.h"
#include "actuators_lib.h"
#include "driver/ledc.h"
#include <inttypes.h>
#include <esp_err.h>
#include "log_lib.h"

static const char *TAG = "buzzer_library";

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

#if CONFIG_USE_KY006

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

    log_msg(TAG, "Buzzer (KY006) initialized");
    return ESP_OK;
}

esp_err_t close_buzzer(void) {
    esp_err_t err;
    esp_err_t first_error = ESP_OK;

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

    log_msg(TAG, "Applied duty %" PRIu32, duty);
    return ESP_OK;
}

#else // !CONFIG_USE_KY006

esp_err_t init_buzzer(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t close_buzzer(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t ledc_buzzer(int16_t buzzer_percent) { (void)buzzer_percent; return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_KY006
