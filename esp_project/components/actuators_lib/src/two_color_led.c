#include "two_color_led.h"
#include "actuators_lib.h"
#include "driver/ledc.h"
#include <inttypes.h>
#include <esp_err.h>
#include "log_lib.h"

static const char *TAG = "two_color_led_library";

#if !CONFIG_IDF_TARGET_ESP32C6

ledc_timer_config_t ledc_timer_ky029 = {
    .duty_resolution = KY029_RESOLUTION,
    .freq_hz = KY029_FREQ,
    .speed_mode = KY029_SPEED_MODE,
    .timer_num = KY029_LEDC_TIMER,
    .clk_cfg = LEDC_AUTO_CLK,
};

static ledc_channel_config_t ledc_channel_ky029_red = {
    .channel = KY029_RED_CHANNEL,
    .duty = 0,
    .gpio_num = KY029_RED_GPIO,
    .speed_mode = KY029_SPEED_MODE,
    .hpoint = 0,
    .timer_sel = KY029_LEDC_TIMER,
    .flags.output_invert = 0,
};

static ledc_channel_config_t ledc_channel_ky029_green = {
    .channel = KY029_GREEN_CHANNEL,
    .duty = 0,
    .gpio_num = KY029_GREEN_GPIO,
    .speed_mode = KY029_SPEED_MODE,
    .hpoint = 0,
    .timer_sel = KY029_LEDC_TIMER,
    .flags.output_invert = 0,
};

#if CONFIG_USE_KY029

esp_err_t init_two_color_led(void) {
    esp_err_t err;

    err = ledc_timer_config(&ledc_timer_ky029);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) configuring KY029 timer", esp_err_to_name(err));
        return err;
    }

    err = ledc_channel_config(&ledc_channel_ky029_red);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) configuring KY029 red channel", esp_err_to_name(err));
        return err;
    }

    err = ledc_channel_config(&ledc_channel_ky029_green);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) configuring KY029 green channel", esp_err_to_name(err));
        return err;
    }

    log_msg(TAG, "Two-color LED (KY029) initialized");
    return ESP_OK;
}

esp_err_t close_two_color_led(void) {
    esp_err_t err;
    esp_err_t first_error = ESP_OK;

    err = ledc_timer_pause(ledc_timer_ky029.speed_mode, ledc_timer_ky029.timer_num);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) pausing KY029 timer", esp_err_to_name(err));
        first_error = err;
    }

    err = ledc_timer_rst(ledc_timer_ky029.speed_mode, ledc_timer_ky029.timer_num);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting KY029 timer", esp_err_to_name(err));
        if (first_error == ESP_OK) first_error = err;
    }

    return first_error;
}

esp_err_t ledc_ky029(int16_t ky029_percent, bool red) {
    if (ky029_percent < 0) ky029_percent = 0;
    if (ky029_percent > 100) ky029_percent = 100;

    uint32_t duty = KY029_MIN_DUTY + ((KY029_MAX_DUTY - KY029_MIN_DUTY) * ky029_percent) / 100;
    esp_err_t err = ledc_apply_duty(KY029_SPEED_MODE, red ? KY029_RED_CHANNEL : KY029_GREEN_CHANNEL, duty);
    if (err != ESP_OK) {
        return err;
    }

    log_msg(TAG, "Applied duty %" PRIu32 " on %s channel", duty, red ? "red" : "green");
    return ESP_OK;
}

#else // !CONFIG_USE_KY029

esp_err_t init_two_color_led(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t close_two_color_led(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t ledc_ky029(int16_t ky029_percent, bool red) { (void)ky029_percent; (void)red; return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_KY029

#endif
