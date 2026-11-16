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
#include "hcsr04.h"
#include "sensors_lib.h"
#include "ky033.h"

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
    _Atomic uint8_t curve_type;
    _Atomic uint8_t accel_param;  // 0-255, meaning depends on curve_type
    _Atomic uint8_t decel_param;  // separate from accel_param (accel/braking asymmetry)
} drive_profile_config_t;

#define MOTOR_FRAME_SIZE 8
#define BREAKING_FRAME_SIZE (sizeof(uint8_t) + sizeof(uint32_t) + 2 * sizeof(uint16_t) + sizeof(int16_t)) // 11

static atomic_bool breaking_lock = false;

static _Atomic int16_t current_motor = 0;
static _Atomic int16_t target_motor = 0;
static atomic_bool last_current_motor_sign_positive = false;
static _Atomic uint32_t timestamp_force_motor_ms = 0;
static _Atomic uint32_t timeout_breaking = 0;

static drive_profile_config_t cfg = { CURVE_LINEAR, 30, 30 };
static _Atomic int32_t ramp_start_motor = 0;
static _Atomic uint32_t ramp_tick = 0;

static _Atomic uint8_t drive_mode = DRIVE_RAW;
_Atomic float target_speed_m_s = 0;

static float integ;
static int8_t prev_sign;

static atomic_bool brake_held = false;

void set_brake_held(bool held) {
    atomic_store(&brake_held, held);
}

static int16_t speed_pi(float target, float meas, float cap_abs) {
    const float kp = 100.0f, ki = 200.0f;
    int8_t sign = (target >= 0.0f) ? 1 : -1;
    if (sign != prev_sign) { integ = 0.0f; prev_sign = sign; }

    float dt = 0.020f;

    float err = target - meas;
    integ += err * dt;
    float u = kp * err + ki * integ;
    float lim = cap_abs * 1000.0f;
    if (u > lim) { u = lim; integ -= err * dt; }
    else if (u < -lim) { u = -lim; integ -= err * dt; }
    return (int16_t)u;
}

esp_err_t set_drive_mode(drive_mode_t mode) {
    if (mode > DRIVE_SPEED) return ESP_ERR_INVALID_ARG;
    if (mode != atomic_load(&drive_mode)) integ = 0.0f;
    atomic_store(&drive_mode, mode);
    return ESP_OK;
}

/**
 * Serialize the current drive profile + motor state into a telemetry frame.
 * Layout: [curve_type][accel_param][decel_param][current_motor:i16][target_motor:i16][hc_block_activated]
 */
static void serialize_motor(const drive_profile_config_t *drive_cfg, uint8_t *buf) {
    uint8_t curve_type = atomic_load(&drive_cfg->curve_type);
    uint8_t accel_param = atomic_load(&drive_cfg->accel_param);
    uint8_t decel_param = atomic_load(&drive_cfg->decel_param);
    buf[0] = curve_type;
    buf[1] = accel_param;
    buf[2] = decel_param;
    int16_t curr = atomic_load(&current_motor);
    int16_t targ = atomic_load(&target_motor);
    memcpy(&buf[3], &curr, sizeof(int16_t));
    memcpy(&buf[5], &targ, sizeof(int16_t));

    bool hc_block_activated = atomic_load(&hc_locked_config);
    buf[7] = (uint8_t)hc_block_activated;
}

/**
 * Serialize an emergency braking event into a telemetry frame.
 * Layout: [breaking][timeout_us:u32][pulses_100ms:u16][pulses_20ms:u16][current_motor:i16]
 */
static void serialize_breaking(uint8_t *buf, bool breaking, uint16_t pulses_100ms, uint16_t pulses_20ms) {
    buf[0] = (uint8_t)breaking;
    uint32_t val_timeout = atomic_load(&timeout_breaking);
    memcpy(&buf[1], &val_timeout, sizeof(uint32_t));
    memcpy(&buf[5], &pulses_100ms, sizeof(uint16_t));
    memcpy(&buf[7], &pulses_20ms, sizeof(uint16_t));
    int16_t curr = atomic_load(&current_motor);
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
            atomic_fetch_add(&ramp_tick, 1);
            uint32_t total_ticks = 200 - param; // higher param = shorter/sharper ramp
            if (total_ticks < 1) {
                total_ticks = 1;
            }
            uint32_t val_tick = atomic_load(&ramp_tick);
            float t = (float)val_tick / total_ticks;
            if (t > 1.0f) {
                t = 1.0f;
            }
            float s = (1.0f - cosf((float)M_PI * t)) / 2.0f;
            int32_t start_motor = atomic_load(&ramp_start_motor);
            return start_motor + (int32_t)((target - start_motor) * s);
        }
    }
    return current;
}

/**
 * target_motor > 0 is forward, < 0 is reverse (after the sign flip in ledc_motor).
 * Front HC limits forward, rear HC limits reverse. Emergency braking is not capped.
 * The stored command is left intact: only the ramp destination changes.
 */
static int16_t limit_target_by_hc(int16_t target) {
    if (!atomic_load(&hc_locked_config) || atomic_load(&breaking_lock)) {
        return target;
    }

    uint16_t cap = 1000;
    if (target > 0 && get_front_duty_cap(&cap) == ESP_OK) {
        if (target > (int16_t)cap) target = (int16_t)cap;
    } else if (target < 0 && get_rear_duty_cap(&cap) == ESP_OK) {
        if (target < -(int16_t)cap) target = -(int16_t)cap;
    }
    return target;
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
    static int32_t last_target = 0;

    int16_t val_target = atomic_load(&target_motor);
    int16_t val_current = atomic_load(&current_motor);
    if (abs(val_target - last_target) > 15) {
        atomic_store(&ramp_tick, 0);
        atomic_store(&ramp_start_motor, val_current);
        last_target = val_target;
    }

    int16_t applied = val_current;
    drive_mode_t drive = atomic_load(&drive_mode);


    if (atomic_load(&breaking_lock)) {
        uint16_t pulses = 0;
        uint32_t dt_ms = (uint32_t)(esp_timer_get_time() / 1000) - atomic_load(&timestamp_force_motor_ms);
        static uint8_t stopped_windows = 0;

        uint32_t val_timeout = atomic_load(&timeout_breaking);
        uint32_t timeout_ms = val_timeout / 1000u;
        if (timeout_ms < 1) timeout_ms = 1;

        bool pulses_valid = (get_pulses_count_20ms(&pulses) == ESP_OK);

        if (pulses_valid && pulses < 2) {
            if (stopped_windows < 5) {
                stopped_windows++;
            }
        } else {
            stopped_windows = 0;
        }
        bool real_stop = (stopped_windows >= 5);

        if (dt_ms > timeout_ms || real_stop) {
            log_msg_lvl(ESP_LOG_WARN, TAG, "Braking stopped [%s] 20ms pulses: %u, dt_ms: %u, timeout_ms: %u",
                real_stop ? "pulse" : "timeout", pulses, dt_ms, timeout_ms);
            atomic_store(&target_motor, 0);
            atomic_store(&breaking_lock, false);
            integ = 0.0f;
            applied = 0;

#if CONFIG_USE_UDPLIB && CONFIG_USE_SENSORS
            uint16_t pulses_100ms = 0, pulses_20ms = 0;
            get_pulses_count_100ms(&pulses_100ms);
            get_pulses_count_20ms(&pulses_20ms);
            send_braking_telemetry(false, pulses_100ms, pulses_20ms);
#endif
        } else {
            bool is_accel = (val_target > applied && applied >= 0) ||
                            (val_target < applied && applied <= 0);
            uint8_t curve_type = atomic_load((curve_type_t*)&cfg.curve_type);
            uint8_t accel_param = atomic_load(&cfg.accel_param);
            uint8_t decel_param = atomic_load(&cfg.decel_param);
            applied = (int16_t)apply_curve_step(applied, val_target,
                is_accel ? accel_param : decel_param, curve_type);
        }
    } else if (drive == DRIVE_RAW) {
        applied = val_target;
    } else if (atomic_load(&brake_held)) {
        integ = 0.0f;
        applied = 0;
    } else if (drive == DRIVE_SPEED) {
        float target = atomic_load(&target_speed_m_s);
        static uint32_t seen_id;
        static int16_t last_duty;

        uint32_t id = 0;
        float mag = 0.0f;
        get_wheel_sample(&id, &mag);
        float meas = (target >= 0.0f) ? mag : -mag;

        if (fabsf(target) < 0.02f) {
            integ = 0.0f;
            applied = 0;
            last_duty = 0;
            seen_id = id;
        } else if (id != seen_id) {
            seen_id = id;
            uint16_t cap = 1000;
            //if (target > 0.0f) get_front_duty_cap(&cap);
            //else get_rear_duty_cap(&cap);
            applied = speed_pi(target, meas, cap / 1000.0f);
            last_duty = applied;

            log_msg(TAG, "PI; target: %.2f, applied: %d, meas: %.2f, ids:%u/%u", 
                target, applied, meas, seen_id, id);
        } else {
            applied = last_duty;
        }
    } else {
        val_target = limit_target_by_hc(val_target);
        bool is_accel = (val_target > applied && applied >= 0) ||
                        (val_target < applied && applied <= 0);
        uint8_t curve_type = atomic_load((curve_type_t*)&cfg.curve_type);
        uint8_t accel_param = atomic_load(&cfg.accel_param);
        uint8_t decel_param = atomic_load(&cfg.decel_param);
        applied = (int16_t)apply_curve_step(applied, val_target,
            is_accel ? accel_param : decel_param, curve_type);
    }

    if (applied != val_current) {
        atomic_store(&current_motor, applied);

        if (applied > 0) {
            ledc_apply_duty(BTS_SPEED_MODE, BTS_CHANNEL_FWD,
                MIN_MOTOR_DUTY_FWD + ((MAX_MOTOR_DUTY_FWD - MIN_MOTOR_DUTY_FWD) * applied) / 1000);
            ledc_apply_duty(BTS_SPEED_MODE, BTS_CHANNEL_BWD, 0);
            atomic_store(&last_current_motor_sign_positive, true);
        } else if (applied < 0) {
            ledc_apply_duty(BTS_SPEED_MODE, BTS_CHANNEL_BWD,
                MIN_MOTOR_DUTY_BWD + ((MAX_MOTOR_DUTY_BWD - MIN_MOTOR_DUTY_BWD) * -applied) / 1000);
            ledc_apply_duty(BTS_SPEED_MODE, BTS_CHANNEL_FWD, 0);
            atomic_store(&last_current_motor_sign_positive, false);
        } else {
            ledc_apply_duty(BTS_SPEED_MODE, BTS_CHANNEL_FWD, 
                MIN_MOTOR_DUTY_FWD + ((MAX_MOTOR_DUTY_FWD - MIN_MOTOR_DUTY_FWD) * 1000) / 1000);
            ledc_apply_duty(BTS_SPEED_MODE, BTS_CHANNEL_BWD, 
                MIN_MOTOR_DUTY_BWD + ((MAX_MOTOR_DUTY_BWD - MIN_MOTOR_DUTY_BWD) * 1000) / 1000);
        }

        log_msg(TAG, "Motor: %d/%d, drive: %d",
            applied, val_target, drive);

#if CONFIG_WRITE_MOTOR_SCREEN
        char tmp[30];
        snprintf(tmp, sizeof(tmp), "Motor: %d, Target: %d", applied, val_target);
        ssd1306_draw_string(tmp, 0, 4);
#endif
    } else {
        atomic_store(&ramp_tick, 0);
        atomic_store(&ramp_start_motor, 0);
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

    atomic_store(&cfg.curve_type, buf[0]);
    atomic_store(&cfg.accel_param, buf[1]);
    atomic_store(&cfg.decel_param, buf[2]);
    atomic_store(&ramp_tick, 0);
    int16_t val_current = atomic_load(&current_motor);
    atomic_store(&ramp_start_motor, (int32_t)val_current);
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
    atomic_store(&target_motor, motor);
    return ESP_OK;
}

esp_err_t get_motor_percent(int16_t *motor) {
    if (motor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *motor = atomic_load(&current_motor);
    return ESP_OK;
}

esp_err_t get_last_motor_sign_positive(bool *sign) {
    if (sign == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *sign = atomic_load(&last_current_motor_sign_positive);
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

    atomic_store(&timestamp_force_motor_ms, (uint32_t)(esp_timer_get_time() / 1000));

    int16_t val_current = atomic_load(&current_motor);
    uint32_t timeout_to_store = 0;
    if (abs(val_current) < 100 && pulses > 0) {
        // Throttle released but inertia is still carrying the vehicle. Value is microseconds.
        timeout_to_store = (pulses * 8 + 200) * 800;
    } else {
        timeout_to_store = (uint32_t)abs(val_current) * 800;
    }
    atomic_store(&timeout_breaking, timeout_to_store);

    bool last_sign = atomic_load(&last_current_motor_sign_positive);
    log_msg_lvl(ESP_LOG_WARN, TAG,
        "Braking started, pulses: %u, current_motor: %d, timeout_us: %" PRIu32 ", last_sign: %s",
        pulses, val_current, timeout_to_store,
        last_sign ? "+" : "-");

    atomic_store(&breaking_lock, true);
    set_motor_percent(-val_current);
    buzzer_play(BUZZER_PATTERN_EMERGENCY);

#if CONFIG_USE_UDPLIB && CONFIG_USE_SENSORS
    uint16_t pulses_20ms = 0;
    get_pulses_count_20ms(&pulses_20ms);
    send_braking_telemetry(true, pulses, pulses_20ms);
#endif

    return ESP_OK;
}

esp_err_t ledc_motor(int16_t motor_percent) {
    if (atomic_load(&breaking_lock)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (motor_percent < -1000) motor_percent = -1000;
    if (motor_percent > 1000) motor_percent = 1000;

    atomic_store(&target_motor, motor_percent);
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
esp_err_t ledc_motor(int16_t motor_percent) { (void)motor_percent; return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_BTS7960