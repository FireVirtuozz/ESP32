#ifndef BUZZER_H_
#define BUZZER_H_

#include <inttypes.h>
#include <esp_err.h>
#include <stdbool.h>
#include <stddef.h>
#include "driver/ledc.h"

// KY-006: passive buzzer, pitch driven by PWM frequency

#define BUZZER_GPIO 4
#define BUZZER_RESOLUTION LEDC_TIMER_13_BIT
#define BUZZER_START_FREQ 2000
#define BUZZER_MIN_DUTY 0
#define BUZZER_MAX_DUTY (GET_MAX_DUTY(BUZZER_RESOLUTION) / 2)
#define BUZZER_SPEED_MODE LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_TIMER LEDC_TIMER_2
#define BUZZER_LEDC_CHANNEL LEDC_CHANNEL_4

/**
 * Short, non-blocking sound cues used by the vehicle state machine.
 * A newly requested cue interrupts the cue currently playing.
 */
typedef enum {
    BUZZER_PATTERN_READY,
    BUZZER_PATTERN_OBSTACLE_FRONT,
    BUZZER_PATTERN_OBSTACLE_REAR,
    BUZZER_PATTERN_EMERGENCY,
    BUZZER_PATTERN_HORN,
    BUZZER_PATTERN_STOP,
} buzzer_pattern_t;

extern ledc_timer_config_t ledc_timer_buzzer;

/**
 * Configure the buzzer's LEDC timer and channel.
 */
esp_err_t init_buzzer(void);

/**
 * Pause and reset the buzzer's LEDC timer.
 */
esp_err_t close_buzzer(void);

/**
 * Apply a buzzer intensity, clamped to [0, 100] percent.
 */
esp_err_t ledc_buzzer(int16_t buzzer_percent);

/** Start a sound cue without blocking the calling task. */
esp_err_t buzzer_play(buzzer_pattern_t pattern);

/** Stop the current cue immediately. */
esp_err_t buzzer_stop(void);

#endif // BUZZER_H_
