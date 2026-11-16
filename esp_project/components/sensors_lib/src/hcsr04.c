#include "hcsr04.h"
#include "sensors_lib.h"
#include "log_lib.h"

#if CONFIG_USE_UDPLIB
#include "udp_lib.h"
#endif

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include <string.h>
#include <stdatomic.h>
#include "buzzer.h"

static const char *TAG = "hcsr04_sensor";
atomic_bool hc_locked_config = ATOMIC_VAR_INIT(false);

#if CONFIG_USE_HCSR04

#define HCSR04_FRONT_TRIG 40
#define HCSR04_FRONT_ECHO 21
#define HCSR04_REAR_TRIG  16
#define HCSR04_REAR_ECHO  14
#define HCSR04_FULL_CM    80.0f
#define HCSR04_STOP_CM    15.0f
#define HCSR04_CLEAR_CM   25.0f
#define HCSR04_TIMEOUT_CM 400.0f

typedef struct {
    hcsr04_config_t cfg;
    SemaphoreHandle_t echo_sem;
    volatile int64_t echo_start_us;
    volatile int64_t echo_end_us;
    volatile bool blocked;
    _Atomic uint16_t duty_cap_milli;
    uint8_t miss;
} hcsr04_ctx_t;

static hcsr04_ctx_t front_ctx = { .cfg = { .hc_id = 0, .trig_pin = HCSR04_FRONT_TRIG, .echo_pin = HCSR04_FRONT_ECHO } };
static hcsr04_ctx_t rear_ctx  = { .cfg = { .hc_id = 1, .trig_pin = HCSR04_REAR_TRIG,  .echo_pin = HCSR04_REAR_ECHO } };

static void IRAM_ATTR echo_isr_handler(void *arg) {
    hcsr04_ctx_t *ctx = (hcsr04_ctx_t *)arg;
    if (gpio_get_level(ctx->cfg.echo_pin)) {
        ctx->echo_start_us = esp_timer_get_time();
    } else {
        ctx->echo_end_us = esp_timer_get_time();
        BaseType_t task_awoken = pdFALSE;
        xSemaphoreGiveFromISR(ctx->echo_sem, &task_awoken);
        portYIELD_FROM_ISR(task_awoken);
    }
}

static esp_err_t init_hcsr04_gpio(hcsr04_ctx_t *ctx) {
    esp_err_t err = gpio_reset_pin(ctx->cfg.trig_pin);
    if (err != ESP_OK) return err;
    err = gpio_set_direction(ctx->cfg.trig_pin, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) return err;
    err = gpio_set_level(ctx->cfg.trig_pin, 0);
    if (err != ESP_OK) return err;

    err = gpio_reset_pin(ctx->cfg.echo_pin);
    if (err != ESP_OK) return err;
    err = gpio_set_direction(ctx->cfg.echo_pin, GPIO_MODE_INPUT);
    if (err != ESP_OK) return err;
    err = gpio_set_intr_type(ctx->cfg.echo_pin, GPIO_INTR_ANYEDGE);
    if (err != ESP_OK) return err;

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    return gpio_isr_handler_add(ctx->cfg.echo_pin, echo_isr_handler, ctx);
}

static int64_t trigger_echo(hcsr04_ctx_t *ctx) {
    while (xSemaphoreTake(ctx->echo_sem, 0) == pdTRUE) {}
    ctx->echo_start_us = 0;
    ctx->echo_end_us = 0;

    gpio_set_level(ctx->cfg.trig_pin, 1);
    esp_rom_delay_us(10);
    gpio_set_level(ctx->cfg.trig_pin, 0);

    if (xSemaphoreTake(ctx->echo_sem, pdMS_TO_TICKS(60)) != pdTRUE) {
        return INT64_MIN;
    }

    int64_t width_us = ctx->echo_end_us - ctx->echo_start_us;
    if (width_us < 0) {
        return INT64_MIN;
    }
    return width_us;
}

static uint16_t cap_milli_from_cm(hcsr04_ctx_t *ctx, float dist_cm) {
    if (ctx->blocked) {
        if (dist_cm < HCSR04_CLEAR_CM) return 0;
        ctx->blocked = false;
    }
    if (dist_cm <= HCSR04_STOP_CM) {
        if (!ctx->blocked) {
            ctx->blocked = true;
            buzzer_play(ctx->cfg.hc_id == 0
                ? BUZZER_PATTERN_OBSTACLE_FRONT
                : BUZZER_PATTERN_OBSTACLE_REAR);
        }
        return 0;
    }
    if (dist_cm >= HCSR04_FULL_CM) return 1000;
    float t = (dist_cm - HCSR04_STOP_CM) / (HCSR04_FULL_CM - HCSR04_STOP_CM);
    return (uint16_t)(t * 1000.0f);
}

static void send_hcsr04_packet(hcsr04_ctx_t *ctx, int64_t val_hc) {
    header_sensor_t header = {0};
    header.esp_id = (uint8_t)CONFIG_ESP_ID;
    header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
    header.type = SENSOR_TYPE_HCSR04;
    uint8_t buf[HEADER_SENSOR_SIZE + sizeof(int64_t) + 2];
    serialize_header(&header, buf);
    buf[HEADER_SENSOR_SIZE] = ctx->cfg.hc_id;
    memcpy(&buf[HEADER_SENSOR_SIZE + 1], &val_hc, sizeof(int64_t));
    buf[HEADER_SENSOR_SIZE + 1 + sizeof(int64_t)] = (uint8_t)ctx->blocked;
#if CONFIG_USE_UDPLIB
    send_udp_sensor(buf, sizeof(buf));
#endif
}

static void hcsr04_task(void *params) {
    hcsr04_ctx_t *ctx = (hcsr04_ctx_t *)params;

    atomic_store(&ctx->duty_cap_milli, 1000);
    ctx->echo_sem = xSemaphoreCreateBinary();
    if (ctx->echo_sem == NULL || init_hcsr04_gpio(ctx) != ESP_OK) {
        log_msg(TAG, "Error initializing HC-SR04 id %d", ctx->cfg.hc_id);
        vTaskDelete(NULL);
        return;
    }
    log_msg(TAG, "HC-SR04 id %d initialized (trig: %d, echo: %d)",
        ctx->cfg.hc_id, ctx->cfg.trig_pin, ctx->cfg.echo_pin);

    while (true) {
        int64_t val_hc = trigger_echo(ctx);
        bool timeout = (val_hc == INT64_MIN);

        if (timeout && atomic_load(&hc_locked_config)) {
            ctx->miss++;
            if (ctx->miss < 3) {
                send_hcsr04_packet(ctx, (int64_t)(HCSR04_TIMEOUT_CM * 58.0f));
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
        }
        ctx->miss = 0;

        if (!atomic_load(&hc_locked_config) || timeout) {
            if (ctx->blocked) {
                log_msg_lvl(ESP_LOG_WARN, TAG, "HC-SR04 id %d unblocked (%s)",
                    ctx->cfg.hc_id, timeout ? "echo timeout" : "system off");
            }
            ctx->blocked = false;
            atomic_store(&ctx->duty_cap_milli, 1000);
            if (timeout) {
                val_hc = (int64_t)(HCSR04_TIMEOUT_CM * 58.0f);
            }
        } else {
            float dist_cm = (float)val_hc / 58.0f;
            bool was_blocked = ctx->blocked;
            uint16_t cap = cap_milli_from_cm(ctx, dist_cm);
            atomic_store(&ctx->duty_cap_milli, cap);
            if (!was_blocked && ctx->blocked) {
                log_msg_lvl(ESP_LOG_WARN, TAG, "HC-SR04 id %d cap 0 at %.2f cm", ctx->cfg.hc_id, dist_cm);
            } else if (was_blocked && !ctx->blocked) {
                log_msg_lvl(ESP_LOG_WARN, TAG, "HC-SR04 id %d cap released at %.2f cm", ctx->cfg.hc_id, dist_cm);
            }
        }

        send_hcsr04_packet(ctx, val_hc);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t init_hcsr04(void) {
    atomic_store(&front_ctx.duty_cap_milli, 1000);
    atomic_store(&rear_ctx.duty_cap_milli, 1000);

    esp_err_t err = xTaskCreate(hcsr04_task, "hcsr04_front_task", 3072, &front_ctx, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(25));
    return xTaskCreate(hcsr04_task, "hcsr04_rear_task", 3072, &rear_ctx, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t get_front_blocked(bool *blocked) {
    if (blocked == NULL) return ESP_ERR_INVALID_ARG;
    *blocked = front_ctx.blocked;
    return ESP_OK;
}

esp_err_t get_rear_blocked(bool *blocked) {
    if (blocked == NULL) return ESP_ERR_INVALID_ARG;
    *blocked = rear_ctx.blocked;
    return ESP_OK;
}

esp_err_t get_front_duty_cap(uint16_t *cap_milli) {
    if (cap_milli == NULL) return ESP_ERR_INVALID_ARG;
    *cap_milli = atomic_load(&front_ctx.duty_cap_milli);
    return ESP_OK;
}

esp_err_t get_rear_duty_cap(uint16_t *cap_milli) {
    if (cap_milli == NULL) return ESP_ERR_INVALID_ARG;
    *cap_milli = atomic_load(&rear_ctx.duty_cap_milli);
    return ESP_OK;
}

#else // !CONFIG_USE_HCSR04

esp_err_t init_hcsr04(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t get_front_blocked(bool *blocked) { if (blocked) *blocked = false; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t get_rear_blocked(bool *blocked) { if (blocked) *blocked = false; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t get_front_duty_cap(uint16_t *cap_milli) { if (cap_milli) *cap_milli = 1000; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t get_rear_duty_cap(uint16_t *cap_milli) { if (cap_milli) *cap_milli = 1000; return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_HCSR04