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

static const char *TAG = "hcsr04_sensor";

#if CONFIG_USE_HCSR04

#define HCSR04_FRONT_TRIG 32
#define HCSR04_FRONT_ECHO 33
#define HCSR04_REAR_TRIG  25
#define HCSR04_REAR_REAR  26
#define HCSR04_BLOCK_DISTANCE_CM 10.0
#define HCSR04_BLOCK_APPROACH_CM (-2.0)

typedef struct {
    hcsr04_config_t cfg;
    SemaphoreHandle_t echo_sem;
    volatile int64_t echo_start_us;
    volatile int64_t echo_end_us;
    volatile bool blocked;
    volatile float last_distance_cm;
} hcsr04_ctx_t;

static hcsr04_ctx_t front_ctx = { .cfg = { .hc_id = 0, .trig_pin = HCSR04_FRONT_TRIG, .echo_pin = HCSR04_FRONT_ECHO } };
static hcsr04_ctx_t rear_ctx  = { .cfg = { .hc_id = 1, .trig_pin = HCSR04_REAR_TRIG,  .echo_pin = HCSR04_REAR_REAR } };

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

/**
 * Trigger a measurement and block until the echo pulse width is known.
 * Returns the RAW echo width in microseconds (int64_t) — this is the exact
 * wire value the original firmware sent (val_hc); PC-side and any local
 * blocking logic divide by 58.0 themselves to get centimeters. Returns
 * INT64_MIN on timeout (no obstacle in range / no echo).
 */
static int64_t trigger_echo(hcsr04_ctx_t *ctx) {
    gpio_set_level(ctx->cfg.trig_pin, 1);
    esp_rom_delay_us(10);
    gpio_set_level(ctx->cfg.trig_pin, 0);

    if (xSemaphoreTake(ctx->echo_sem, pdMS_TO_TICKS(60)) != pdTRUE) {
        return INT64_MIN; // timeout: no echo, treat as "no obstacle in range"
    }

    int64_t width_us = ctx->echo_end_us - ctx->echo_start_us;
    if (width_us < 0) {
        return INT64_MIN;
    }
    return width_us;
}

static void hcsr04_task(void *params) {
    hcsr04_ctx_t *ctx = (hcsr04_ctx_t *)params;

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

        if (val_hc != INT64_MIN) {
            float dist_cm = (float)val_hc / 58.0f; // used locally for blocking logic only

            // Block if getting closer fast, or already very close.
            bool approaching = (dist_cm - ctx->last_distance_cm) < HCSR04_BLOCK_APPROACH_CM;
            bool very_close = dist_cm < HCSR04_BLOCK_DISTANCE_CM;

            if (!ctx->blocked && (approaching || very_close)) {
                ctx->blocked = true;
                log_msg_lvl(ESP_LOG_WARN, TAG, "HC-SR04 id %d blocked at %.2f cm", ctx->cfg.hc_id, dist_cm);
            }
            if (ctx->blocked && dist_cm > HCSR04_BLOCK_DISTANCE_CM) {
                ctx->blocked = false;
                log_msg_lvl(ESP_LOG_WARN, TAG, "HC-SR04 id %d unblocked at %.2f cm", ctx->cfg.hc_id, dist_cm);
            }
            ctx->last_distance_cm = dist_cm;

            // Wire format preserved exactly from the original firmware:
            // [hc_id: u8][val_hc raw echo width: i64][blocked: u8]
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
        } else if (ctx->blocked) {
            // Echo timeout: on real hardware this almost always means
            // "nothing within ~4m range", i.e. clearly not an obstacle.
            // Unlike the original (which nested the unblock check inside
            // the valid-reading branch and could stay latched "blocked"
            // forever if the obstacle left range faster than a fresh valid
            // reading arrived), a timeout here explicitly clears it.
            ctx->blocked = false;
            log_msg_lvl(ESP_LOG_WARN, TAG, "HC-SR04 id %d unblocked (echo timeout, out of range)", ctx->cfg.hc_id);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t init_hcsr04(void) {
    esp_err_t err = xTaskCreate(hcsr04_task, "hcsr04_front_task", 3072, &front_ctx, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(25)); // stagger, avoid simultaneous ultrasonic triggers
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

#else // !CONFIG_USE_HCSR04

esp_err_t init_hcsr04(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t get_front_blocked(bool *blocked) { if (blocked) *blocked = false; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t get_rear_blocked(bool *blocked) { if (blocked) *blocked = false; return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_HCSR04