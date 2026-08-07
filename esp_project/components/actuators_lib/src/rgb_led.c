#include "rgb_led.h"
#include "actuators_lib.h"
#include "driver/ledc.h"
#include <inttypes.h>
#include <esp_err.h>
#include "log_lib.h"

static const char *TAG = "rgb_led_library";

#if !CONFIG_IDF_TARGET_ESP32C6

ledc_timer_config_t ledc_timer_ky009 = {
    .duty_resolution = KY009_RESOLUTION,
    .freq_hz = KY009_FREQ,
    .speed_mode = KY009_SPEED_MODE,
    .timer_num = KY009_LEDC_TIMER,
    .clk_cfg = LEDC_AUTO_CLK,
};

static ledc_channel_config_t ledc_channel_ky009_red = {
    .channel = KY009_RED_CHANNEL,
    .duty = 0,
    .gpio_num = KY009_RED_GPIO,
    .speed_mode = KY009_SPEED_MODE,
    .hpoint = 0,
    .timer_sel = KY009_LEDC_TIMER,
    .flags.output_invert = 0,
};

static ledc_channel_config_t ledc_channel_ky009_green = {
    .channel = KY009_GREEN_CHANNEL,
    .duty = 0,
    .gpio_num = KY009_GREEN_GPIO,
    .speed_mode = KY009_SPEED_MODE,
    .hpoint = 0,
    .timer_sel = KY009_LEDC_TIMER,
    .flags.output_invert = 0,
};

static ledc_channel_config_t ledc_channel_ky009_blue = {
    .channel = KY009_BLUE_CHANNEL,
    .duty = 0,
    .gpio_num = KY009_BLUE_GPIO,
    .speed_mode = KY009_SPEED_MODE,
    .hpoint = 0,
    .timer_sel = KY009_LEDC_TIMER,
    .flags.output_invert = 0,
};

#if CONFIG_USE_KY009

esp_err_t init_rgb_led(void) {
    esp_err_t err;

    err = ledc_timer_config(&ledc_timer_ky009);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) configuring KY009 timer", esp_err_to_name(err));
        return err;
    }

    err = ledc_channel_config(&ledc_channel_ky009_red);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) configuring KY009 red channel", esp_err_to_name(err));
        return err;
    }

    err = ledc_channel_config(&ledc_channel_ky009_green);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) configuring KY009 green channel", esp_err_to_name(err));
        return err;
    }

    err = ledc_channel_config(&ledc_channel_ky009_blue);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) configuring KY009 blue channel", esp_err_to_name(err));
        return err;
    }

    log_msg(TAG, "RGB LED (KY009) initialized");
    return ESP_OK;
}

esp_err_t close_rgb_led(void) {
    esp_err_t err;
    esp_err_t first_error = ESP_OK;

    err = ledc_timer_pause(ledc_timer_ky009.speed_mode, ledc_timer_ky009.timer_num);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) pausing KY009 timer", esp_err_to_name(err));
        first_error = err;
    }

    err = ledc_timer_rst(ledc_timer_ky009.speed_mode, ledc_timer_ky009.timer_num);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting KY009 timer", esp_err_to_name(err));
        if (first_error == ESP_OK) first_error = err;
    }

    return first_error;
}

esp_err_t ledc_ky009(int16_t ky009_percent, uint8_t color) {
    if (ky009_percent < 0) ky009_percent = 0;
    if (ky009_percent > 100) ky009_percent = 100;

    ledc_channel_t channel;
    switch (color) {
        case 0: channel = KY009_RED_CHANNEL; break;
        case 1: channel = KY009_GREEN_CHANNEL; break;
        case 2: channel = KY009_BLUE_CHANNEL; break;
        default:
            log_msg(TAG, "Invalid color index %u", color);
            return ESP_ERR_INVALID_ARG;
    }

    uint32_t duty = KY009_MIN_DUTY + ((KY009_MAX_DUTY - KY009_MIN_DUTY) * ky009_percent) / 100;
    esp_err_t err = ledc_apply_duty(KY009_SPEED_MODE, channel, duty);
    if (err != ESP_OK) {
        return err;
    }

    log_msg(TAG, "Applied duty %" PRIu32 " on color index %u", duty, color);
    return ESP_OK;
}

#else // !CONFIG_USE_KY009

esp_err_t init_rgb_led(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t close_rgb_led(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t ledc_ky009(int16_t ky009_percent, uint8_t color) { (void)ky009_percent; (void)color; return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_KY009

#endif
