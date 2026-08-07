#include "servo.h"
#include "actuators_lib.h"
#include "driver/ledc.h"
#include <inttypes.h>
#include <esp_err.h>
#include <stdio.h>
#include "log_lib.h"

#if CONFIG_WRITE_ANGLE_SCREEN
#include "screen_lib.h"
#endif

static const char *TAG = "servo_library";

ledc_timer_config_t ledc_timer_mg = {
    .duty_resolution = MG_RESOLUTION,
    .freq_hz = MG_FREQ,
    .speed_mode = MG_SPEED_MODE,
    .timer_num = MG_TIMER,
    .clk_cfg = LEDC_AUTO_CLK,
};

static ledc_channel_config_t ledc_channel_mg = {
    .channel = MG_CHANNEL,
    .duty = 0,
    .gpio_num = MG_GPIO,
    .speed_mode = MG_SPEED_MODE,
    .hpoint = 0,
    .timer_sel = MG_TIMER,
    .flags.output_invert = 0,
};

#if CONFIG_USE_MG996R

static uint8_t current_angle = 0;

esp_err_t init_servo(void) {
    esp_err_t err;

    err = ledc_timer_config(&ledc_timer_mg);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) configuring MG996R timer", esp_err_to_name(err));
        return err;
    }

    err = ledc_channel_config(&ledc_channel_mg);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) configuring MG996R channel", esp_err_to_name(err));
        return err;
    }

    log_msg(TAG, "Servo (MG996R) initialized");
    return ESP_OK;
}

esp_err_t close_servo(void) {
    esp_err_t err;
    esp_err_t first_error = ESP_OK;

    err = ledc_timer_pause(ledc_timer_mg.speed_mode, ledc_timer_mg.timer_num);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) pausing MG996R timer", esp_err_to_name(err));
        first_error = err;
    }

    err = ledc_timer_rst(ledc_timer_mg.speed_mode, ledc_timer_mg.timer_num);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting MG996R timer", esp_err_to_name(err));
        if (first_error == ESP_OK) first_error = err;
    }

    return first_error;
}

esp_err_t get_servo_angle(uint8_t *angle) {
    if (angle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *angle = current_angle;
    return ESP_OK;
}

esp_err_t ledc_angle(int16_t angle) {
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;

    if (current_angle == angle) {
        // No change: skip redundant duty writes.
        return ESP_OK;
    }
    current_angle = (uint8_t)angle;

    esp_err_t err = ledc_apply_duty(MG_SPEED_MODE, MG_CHANNEL,
        MIN_SERVO_DUTY + ((MAX_SERVO_DUTY - MIN_SERVO_DUTY) * angle) / 180);
    if (err != ESP_OK) {
        return err;
    }

    log_msg(TAG, "Angle: %d, on pin %d", angle, MG_GPIO);

#if CONFIG_WRITE_ANGLE_SCREEN
    char tmp[30];
    snprintf(tmp, sizeof(tmp), "Angle: %d", current_angle);
    ssd1306_draw_string(tmp, 0, 2);
#endif

    return ESP_OK;
}

#else // !CONFIG_USE_MG996R

esp_err_t init_servo(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t close_servo(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t get_servo_angle(uint8_t *angle) { (void)angle; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t ledc_angle(int16_t angle) { (void)angle; return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_MG996R
