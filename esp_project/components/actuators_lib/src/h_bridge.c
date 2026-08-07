#include "h_bridge.h"
#include "actuators_lib.h"
#include "driver/ledc.h"
#include <inttypes.h>
#include <esp_err.h>
#include <stdlib.h>   // abs()
#include <string.h>   // memcpy()
#include <math.h>     // cosf()
#include <stdatomic.h>
#include "esp_timer.h"
#include "log_lib.h"

#if CONFIG_WRITE_MOTOR_SCREEN
#include "screen_lib.h"
#endif

#if CONFIG_USE_UDPLIB && CONFIG_USE_SENSORS
#include "udp_lib.h"
#include "sensors_lib.h"
#endif

static const char *TAG = "h_bridge_library";

ledc_timer_config_t ledc_timer_bts = {
    .duty_resolution = BTS_RESOLUTION,
    .freq_hz = BTS_FREQ,
    .speed_mode = BTS_SPEED_MODE,
    .timer_num = BTS_TIMER,
    .clk_cfg = LEDC_AUTO_CLK,
};

static ledc_channel_config_t ledc_channel_bts_fwd = {
    .channel = BTS_CHANNEL_FWD,
    .duty = 0,
    .gpio_num = BTS_GPIO_FWD,
    .speed_mode = BTS_SPEED_MODE,
    .hpoint = 0,
    .timer_sel = BTS_TIMER,
    .flags.output_invert = 0,
};

static ledc_channel_config_t ledc_channel_bts_bwd = {
    .channel = BTS_CHANNEL_BWD,
    .duty = 0,
    .gpio_num = BTS_GPIO_BWD,
    .speed_mode = BTS_SPEED_MODE,
    .hpoint = 0,
    .timer_sel = BTS_TIMER,
    .flags.output_invert = 0,
};

#if CONFIG_USE_BTS7960

#define MOTOR_CTRL_PERIOD 20000 // microseconds (20ms ramp tick)
#define DEADZONE_MOTOR 50
#define MIN_MOTOR_DUTY_FWD 0
#define MAX_MOTOR_DUTY_FWD GET_MAX_DUTY(BTS_RESOLUTION)
#define MIN_MOTOR_DUTY_BWD 0
#define MAX_MOTOR_DUTY_BWD GET_MAX_DUTY(BTS_RESOLUTION)

typedef enum {
    CURVE_LINEAR = 0,
    CURVE_EXP = 1,
    CURVE_COSINE = 2
} curve_type_t;

typedef struct {
    uint8_t curve_type;
    uint8_t accel_param;  // 0-255, meaning depends on curve_type
    uint8_t decel_param;  // separate from accel_param (accel/braking asymmetry)
} drive_profile_config_t;

#define MOTOR_FRAME_SIZE 8
#define BREAKING_FRAME_SIZE (sizeof(uint8_t) + sizeof(uint32_t) + 2 * sizeof(uint16_t) + sizeof(int16_t)) // 11

static atomic_bool breaking_lock = false;
static volatile bool hc_block_activated = false;

static volatile int16_t current_motor = 0;
static volatile int16_t target_motor = 0;

static int32_t last_target = 0;
static int64_t timestamp_force_motor = 0;
static bool last_current_motor_sign_positive = false;

static drive_profile_config_t cfg = { CURVE_COSINE, 180, 190 };
static int32_t ramp_start_motor = 0;
static uint32_t ramp_tick = 0;
static uint32_t timeout_breaking = 0;

/**
 * Serialize the current drive profile + motor state into a telemetry frame.
 * Layout: [curve_type][accel_param][decel_param][current_motor:i16][target_motor:i16][hc_block_activated]
 */
static void serialize_motor(const drive_profile_config_t *drive_cfg, uint8_t *buf) {
    buf[0] = drive_cfg->curve_type;
    buf[1] = drive_cfg->accel_param;
    buf[2] = drive_cfg->decel_param;
    int16_t curr = current_motor;
    int16_t targ = target_motor;
    memcpy(&buf[3], &curr, sizeof(int16_t));
    memcpy(&buf[5], &targ, sizeof(int16_t));
    buf[7] = (uint8_t)hc_block_activated;
}

/**
 * Serialize an emergency braking event into a telemetry frame.
 * Layout: [breaking][timeout_us:u32][pulses_100ms:u16][pulses_20ms:u16][current_motor:i16]
 */
static void serialize_breaking(uint8_t *buf, bool breaking, uint16_t pulses_100ms, uint16_t pulses_20ms) {
    buf[0] = (uint8_t)breaking;
    memcpy(&buf[1], &timeout_breaking, sizeof(uint32_t));
    memcpy(&buf[5], &pulses_100ms, sizeof(uint16_t));
    memcpy(&buf[7], &pulses_20ms, sizeof(uint16_t));
    int16_t curr = current_motor;
    memcpy(&buf[9], &curr, sizeof(int16_t));
}

#if CONFIG_USE_UDPLIB && CONFIG_USE_SENSORS
/**
 * Build and send a telemetry frame for the current drive profile + motor state.
 */
static void send_motor_telemetry(void) {
    header_sensor_t header = {0};
    header.esp_id = (uint8_t)CONFIG_ESP_ID;
    header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
    header.type = SENSOR_TYPE_MOTOR;
    uint8_t buf[HEADER_SENSOR_SIZE + MOTOR_FRAME_SIZE];
    serialize_header(&header, buf);
    serialize_motor(&cfg, &buf[HEADER_SENSOR_SIZE]);
    send_udp_sensor(buf, sizeof(buf));
}

/**
 * Build and send a telemetry frame for a braking start/stop event.
 */
static void send_braking_telemetry(bool breaking, uint16_t pulses_100ms, uint16_t pulses_20ms) {
    header_sensor_t header = {0};
    header.esp_id = (uint8_t)CONFIG_ESP_ID;
    header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
    header.type = SENSOR_TYPE_BREAK;
    uint8_t buf[HEADER_SENSOR_SIZE + BREAKING_FRAME_SIZE];
    serialize_header(&header, buf);
    serialize_breaking(&buf[HEADER_SENSOR_SIZE], breaking, pulses_100ms, pulses_20ms);
    send_udp_sensor(buf, sizeof(buf));
}
#endif

/**
 * Compute the next ramp value for the motor curve, one control tick at a time.
 *
 * @param current current ramped value
 * @param target  target value the ramp is moving towards
 * @param param   accel_param or decel_param, meaning depends on curve_type
 * @param type    which curve shape to apply
 * @return the next ramped value (clamped so it never overshoots target)
 */
static int32_t apply_curve_step(int32_t current, int32_t target, uint8_t param, curve_type_t type) {
    int32_t delta = target - current;
    if (delta == 0) {
        return current;
    }

    switch (type) {
        case CURVE_LINEAR: {
            int32_t step = (delta > 0) ? param : -param;
            int32_t next = current + step;
            return (delta > 0) ? (next > target ? target : next) : (next < target ? target : next);
        }
        case CURVE_EXP: {
            float alpha = param / 255.0f;
            return current + (int32_t)(delta * alpha);
        }
        case CURVE_COSINE: {
            ramp_tick++;
            uint32_t total_ticks = 200 - param; // higher param = shorter/sharper ramp
            if (total_ticks < 1) {
                total_ticks = 1;
            }
            float t = (float)ramp_tick / total_ticks;
            if (t > 1.0f) {
                t = 1.0f;
            }
            float s = (1.0f - cosf((float)M_PI * t)) / 2.0f;
            return ramp_start_motor + (int32_t)((target - ramp_start_motor) * s);
        }
    }
    return current;
}

/**
 * Periodic ramp-control tick (esp_timer callback, fixed void(*)(void*) signature
 * required by the ESP-IDF esp_timer API — cannot be converted to esp_err_t).
 *
 * Handles, in order: emergency braking supervision, ramp target changes,
 * ramp progression + duty application, and motor telemetry.
 */
static void apply_target_motor(void *args) {
    (void)args;

    if (atomic_load(&breaking_lock)) {
        uint16_t pulses = 0;
        int64_t dt = esp_timer_get_time() - timestamp_force_motor;
        bool real_stop = (get_pulses_count_100ms(&pulses) == ESP_OK && pulses < 2);

        if (dt > timeout_breaking || real_stop) {
            log_msg_lvl(ESP_LOG_WARN, TAG, "Braking stopped [%s] 100ms pulses: %u, dt: %lld",
                dt > timeout_breaking ? "timeout" : "pulse", pulses, dt);
            target_motor = 0;
            atomic_store(&breaking_lock, false);

#if CONFIG_USE_UDPLIB && CONFIG_USE_SENSORS
            uint16_t pulses_100ms = 0, pulses_20ms = 0;
            get_pulses_count_100ms(&pulses_100ms);
            get_pulses_count_20ms(&pulses_20ms);
            send_braking_telemetry(false, pulses_100ms, pulses_20ms);
#endif
        }
    }

    if (target_motor != last_target) {
        ramp_tick = 0;
        ramp_start_motor = current_motor;
        last_target = target_motor;
    }

    if (current_motor != target_motor) {
        bool is_accel = (target_motor > current_motor && current_motor >= 0) ||
                         (target_motor < current_motor && current_motor <= 0);

        current_motor = (int16_t)apply_curve_step(current_motor, target_motor,
            is_accel ? cfg.accel_param : cfg.decel_param, (curve_type_t)cfg.curve_type);

        if (current_motor > 0) {
            ledc_apply_duty(BTS_SPEED_MODE, BTS_CHANNEL_FWD,
                MIN_MOTOR_DUTY_FWD + ((MAX_MOTOR_DUTY_FWD - MIN_MOTOR_DUTY_FWD) * current_motor) / 1000);
            ledc_apply_duty(BTS_SPEED_MODE, BTS_CHANNEL_BWD, 0);
            last_current_motor_sign_positive = true;
        } else if (current_motor < 0) {
            ledc_apply_duty(BTS_SPEED_MODE, BTS_CHANNEL_BWD,
                MIN_MOTOR_DUTY_BWD + ((MAX_MOTOR_DUTY_BWD - MIN_MOTOR_DUTY_BWD) * -current_motor) / 1000);
            ledc_apply_duty(BTS_SPEED_MODE, BTS_CHANNEL_FWD, 0);
            last_current_motor_sign_positive = false;
        } else {
            ledc_apply_duty(BTS_SPEED_MODE, BTS_CHANNEL_FWD, 0);
            ledc_apply_duty(BTS_SPEED_MODE, BTS_CHANNEL_BWD, 0);
        }

        log_msg(TAG, "Motor: %d/%d, on pins; fwd: %d, bwd: %d",
            current_motor, target_motor, BTS_GPIO_FWD, BTS_GPIO_BWD);

#if CONFIG_WRITE_MOTOR_SCREEN
        char tmp[30];
        snprintf(tmp, sizeof(tmp), "Motor: %d, Target: %d", current_motor, target_motor);
        ssd1306_draw_string(tmp, 0, 4);
#endif
    } else {
        ramp_tick = 0;
        ramp_start_motor = current_motor;
    }

#if CONFIG_USE_UDPLIB && CONFIG_USE_SENSORS
    send_motor_telemetry();
#endif
}

esp_err_t apply_config(uint8_t *buf, uint8_t len) {
    if (buf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len < 3) {
        return ESP_ERR_INVALID_SIZE;
    }

    cfg.curve_type = buf[0];
    cfg.accel_param = buf[1];
    cfg.decel_param = buf[2];
    ramp_tick = 0;
    ramp_start_motor = current_motor;
    return ESP_OK;
}

esp_err_t init_bts(void) {
    esp_err_t err;

    err = ledc_timer_config(&ledc_timer_bts);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) configuring BTS7960 timer", esp_err_to_name(err));
        return err;
    }

    err = ledc_channel_config(&ledc_channel_bts_fwd);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) configuring BTS7960 forward channel", esp_err_to_name(err));
        return err;
    }

    err = ledc_channel_config(&ledc_channel_bts_bwd);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) configuring BTS7960 backward channel", esp_err_to_name(err));
        return err;
    }

    log_msg(TAG, "Setting up ramp control timer");
    const esp_timer_create_args_t ctrl_timer_args = {
        .callback = &apply_target_motor,
        .name = "ctrl_timer",
    };
    esp_timer_handle_t ctrl_timer = NULL;
    err = esp_timer_create(&ctrl_timer_args, &ctrl_timer);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) creating ramp control timer", esp_err_to_name(err));
        return err;
    }

    err = esp_timer_start_periodic(ctrl_timer, MOTOR_CTRL_PERIOD);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) starting periodic ramp control timer", esp_err_to_name(err));
        return err;
    }

    log_msg(TAG, "H-Bridge (BTS7960) initialized");
    return ESP_OK;
}

esp_err_t close_bts(void) {
    esp_err_t err;
    esp_err_t first_error = ESP_OK;

    err = ledc_timer_pause(ledc_timer_bts.speed_mode, ledc_timer_bts.timer_num);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) pausing BTS7960 timer", esp_err_to_name(err));
        first_error = err;
    }

    err = ledc_timer_rst(ledc_timer_bts.speed_mode, ledc_timer_bts.timer_num);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting BTS7960 timer", esp_err_to_name(err));
        if (first_error == ESP_OK) first_error = err;
    }

    return first_error;
}

esp_err_t set_motor_percent(int16_t motor) {
    if (motor < -1000) motor = -1000;
    if (motor > 1000) motor = 1000;
    target_motor = motor;
    return ESP_OK;
}

esp_err_t get_motor_percent(int16_t *motor) {
    if (motor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *motor = current_motor;
    return ESP_OK;
}

esp_err_t get_last_motor_sign_positive(bool *sign) {
    if (sign == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *sign = last_current_motor_sign_positive;
    return ESP_OK;
}

esp_err_t force_motor_stop(void) {
    if (atomic_load(&breaking_lock)) {
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t pulses = 0;
    if (get_pulses_count_100ms(&pulses) != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Pulse count unavailable, braking not triggered");
        return ESP_ERR_INVALID_STATE;
    }

    timestamp_force_motor = esp_timer_get_time();

    if (abs(current_motor) < 100 && pulses > 0) {
        // Throttle released but inertia is still carrying the vehicle
        timeout_breaking = (pulses * 8 + 200) * 800;
    } else {
        timeout_breaking = abs(current_motor) * 800;
    }

    log_msg_lvl(ESP_LOG_WARN, TAG,
        "Braking started, pulses: %u, current_motor: %d, timeout: %" PRIu32 ", last_sign: %s",
        pulses, current_motor, timeout_breaking,
        last_current_motor_sign_positive ? "+" : "-");

    if (last_current_motor_sign_positive) {
        set_motor_percent(-1000);
    } else {
        set_motor_percent(1000);
    }
    atomic_store(&breaking_lock, true);

#if CONFIG_USE_UDPLIB && CONFIG_USE_SENSORS
    uint16_t pulses_20ms = 0;
    get_pulses_count_20ms(&pulses_20ms);
    send_braking_telemetry(true, pulses, pulses_20ms);
#endif

    return ESP_OK;
}

esp_err_t activate_hc_blocking(bool active) {
    hc_block_activated = active;
    return ESP_OK;
}

esp_err_t get_active_hc_blocking(bool *active) {
    if (active == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *active = hc_block_activated;
    return ESP_OK;
}

esp_err_t ledc_motor(int16_t motor_percent) {
    if (atomic_load(&breaking_lock)) {
        // Ignored while an emergency braking sequence is in progress.
        return ESP_ERR_INVALID_STATE;
    }

    if (motor_percent < -1000) motor_percent = -1000;
    if (motor_percent > 1000) motor_percent = 1000;

    if (abs(motor_percent) < DEADZONE_MOTOR) {
        motor_percent = 0;
    }

    // get_front_blocked()/get_rear_blocked() gracefully return an error
    // (rather than being compiled out) when HC-SR04 support is disabled,
    // so no CONFIG_ guard is needed here: the check is simply skipped.
    bool front = false, rear = false;
    if (get_front_blocked(&front) == ESP_OK && get_rear_blocked(&rear) == ESP_OK) {
        if ((motor_percent < 0 && front) || (motor_percent > 0 && rear)) {
            log_msg_lvl(ESP_LOG_WARN, TAG, "Cannot %s, blocked by an obstacle",
                motor_percent > 0 ? "reverse" : "forward");
            return ESP_ERR_INVALID_STATE;
        }
    }

    target_motor = -motor_percent;
    return ESP_OK;
}

#else // !CONFIG_USE_BTS7960

esp_err_t init_bts(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t close_bts(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t apply_config(uint8_t *buf, uint8_t len) { (void)buf; (void)len; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t set_motor_percent(int16_t motor) { (void)motor; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t get_motor_percent(int16_t *motor) { (void)motor; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t get_last_motor_sign_positive(bool *sign) { (void)sign; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t force_motor_stop(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t activate_hc_blocking(bool active) { (void)active; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t get_active_hc_blocking(bool *active) { (void)active; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t ledc_motor(int16_t motor_percent) { (void)motor_percent; return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_BTS7960
