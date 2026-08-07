#include "actuators_lib.h"
#include "log_lib.h"

static const char *TAG = "actuators_library";

esp_err_t ledc_apply_duty(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t duty) {
    esp_err_t err;

    err = ledc_set_duty(speed_mode, channel, duty);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting duty %" PRIu32 " on channel %d",
            esp_err_to_name(err), duty, channel);
        return err;
    }

    err = ledc_update_duty(speed_mode, channel);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) updating duty %" PRIu32 " on channel %d",
            esp_err_to_name(err), duty, channel);
        return err;
    }

    log_msg_lvl(ESP_LOG_DEBUG, TAG, "Duty: %" PRIu32 ", on channel %d", duty, channel);
    return ESP_OK;
}

esp_err_t init_all_actuators(void) {
    esp_err_t err;

#if CONFIG_USE_BUILTIN_LED
    err = led_init();
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) initializing simple LED", esp_err_to_name(err));
        return err;
    }
#endif

#if CONFIG_USE_BTS7960
    err = init_bts();
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) initializing H-Bridge (BTS7960)", esp_err_to_name(err));
        return err;
    }
#endif

#if CONFIG_USE_MG996R
    err = init_servo();
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) initializing servo (MG996R)", esp_err_to_name(err));
        return err;
    }
#endif

#if CONFIG_USE_KY006
    err = init_buzzer();
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) initializing buzzer (KY006)", esp_err_to_name(err));
        return err;
    }
#endif

#if CONFIG_USE_KY029
    err = init_two_color_led();
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) initializing two-color LED (KY029)", esp_err_to_name(err));
        return err;
    }
#endif

#if CONFIG_USE_KY009
    err = init_rgb_led();
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) initializing RGB LED (KY009)", esp_err_to_name(err));
        return err;
    }
#endif

#if CONFIG_USE_BUILTIN_LED_W2812B
    err = init_ws2812_rmt();
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) initializing WS2812B RMT driver", esp_err_to_name(err));
        return err;
    }
#endif

    log_msg(TAG, "All enabled actuators initialized");

#if CONFIG_DEBUG_LEDC || CONFIG_DEBUG_GPIO
    print_esp_info_ledc();
#endif

    return ESP_OK;
}

esp_err_t close_all_actuators(void) {
    esp_err_t err;
    esp_err_t first_error = ESP_OK;

#if CONFIG_USE_BTS7960
    err = close_bts();
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) closing H-Bridge (BTS7960)", esp_err_to_name(err));
        if (first_error == ESP_OK) first_error = err;
    }
#endif

#if CONFIG_USE_MG996R
    err = close_servo();
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) closing servo (MG996R)", esp_err_to_name(err));
        if (first_error == ESP_OK) first_error = err;
    }
#endif

#if CONFIG_USE_KY006
    err = close_buzzer();
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) closing buzzer (KY006)", esp_err_to_name(err));
        if (first_error == ESP_OK) first_error = err;
    }
#endif

#if CONFIG_USE_KY029
    err = close_two_color_led();
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) closing two-color LED (KY029)", esp_err_to_name(err));
        if (first_error == ESP_OK) first_error = err;
    }
#endif

#if CONFIG_USE_KY009
    err = close_rgb_led();
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) closing RGB LED (KY009)", esp_err_to_name(err));
        if (first_error == ESP_OK) first_error = err;
    }
#endif

#if CONFIG_USE_BUILTIN_LED
    err = close_simple_led();
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) closing simple LED", esp_err_to_name(err));
        if (first_error == ESP_OK) first_error = err;
    }
#endif

    log_msg(TAG, "All actuators closed%s", first_error == ESP_OK ? "" : " (with errors, see log above)");
    return first_error;
}

esp_err_t init_all_gpios(void) {
    // Deprecated alias, kept so existing call sites keep compiling unchanged.
    return init_all_actuators();
}

esp_err_t close_led(void) {
    // Deprecated alias, kept so existing call sites keep compiling unchanged.
    // Unlike the previous implementation, this now correctly closes the
    // H-Bridge (BTS7960) too, via close_all_actuators().
    return close_all_actuators();
}
