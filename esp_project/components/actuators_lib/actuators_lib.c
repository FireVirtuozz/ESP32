#include "actuators_lib.h"
#include "log_lib.h"
#include "udp_lib.h"
#include <sensors_lib.h>
#include "esp_timer.h"
#include <stdatomic.h>
#include "hcsr04.h"

static const char *TAG = "actuators_library";

#if CONFIG_USE_LEDLIB

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

typedef struct command_pck {
    int16_t throttle;
    int16_t steering;
    uint8_t brake_stop;
    uint8_t hc_block;
    uint8_t horn;
    drive_mode_t drive_mode;
    float target_m_s;
    uint32_t timestamp;
} command_pck_t;

static bool horning = false;
static bool prev_brake = false;

static void deserialise_command_pck(command_pck_t* cmd, uint8_t* buf) {
    memcpy(&cmd->throttle, &buf[0], sizeof(int16_t));
    memcpy(&cmd->steering, &buf[2], sizeof(int16_t));
    cmd->brake_stop = buf[4];
    cmd->hc_block = buf[5];
    cmd->horn = buf[6];
    cmd->drive_mode = buf[7];
    memcpy(&cmd->target_m_s, &buf[8], sizeof(float));
    memcpy(&cmd->timestamp, &buf[12], sizeof(uint32_t));
}


void cmd_dispatch(uint8_t *buf, uint8_t size) {
    if (size != COMMAND_PACKET_SIZE) return;
    command_pck_t cmd = {0};
    deserialise_command_pck(&cmd, buf);

#if CONFIG_USE_UDPLIB
    header_sensor_t header = {0};
    header.esp_id = (uint8_t)CONFIG_ESP_ID;
    header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
    header.type = SENSOR_TYPE_PING;
    uint8_t buf_udp[HEADER_SENSOR_SIZE + sizeof(uint32_t)];
    serialize_header(&header, buf_udp);
    memcpy(&buf_udp[HEADER_SENSOR_SIZE], &cmd.timestamp, sizeof(uint32_t));

    send_udp_sensor(buf_udp, sizeof(buf_udp));
#endif

    if (!horning && cmd.horn) {
        buzzer_play(BUZZER_PATTERN_HORN);
    }
    horning = cmd.horn;

    if (cmd.brake_stop && !prev_brake) force_motor_stop();
    prev_brake = cmd.brake_stop;
    set_brake_held(cmd.brake_stop);
    if (!cmd.brake_stop) ledc_motor(cmd.throttle);
    ledc_angle(cmd.steering);

    atomic_store(&hc_locked_config, cmd.hc_block);
    set_drive_mode(cmd.drive_mode);
    atomic_store(&target_speed_m_s, cmd.target_m_s);
}
#endif

#if !CONFIG_USE_LEDLIB
esp_err_t ledc_apply_duty(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t duty) { return ESP_ERR_INVALID_STATE; }
esp_err_t close_led(void) { return ESP_ERR_INVALID_STATE; }
esp_err_t init_all_actuators(void) { return ESP_ERR_INVALID_STATE; }
esp_err_t init_all_gpios(void) { return ESP_ERR_INVALID_STATE; }
esp_err_t close_all_actuators(void) { return ESP_ERR_INVALID_STATE; }
void cmd_dispatch(uint8_t *buf, uint8_t size) {}
#endif