#include "ledLib.h"
#include "driver/gpio.h"
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvsLib.h"
#include "mqttLib.h"
#include <stdarg.h>
#include <esp_err.h>
#include "driver/ledc.h"
#include "soc/soc_caps.h"

#define LEDC_HS_TIMER          LEDC_TIMER_0
#define LEDC_HS_MODE           LEDC_HIGH_SPEED_MODE
#define LEDC_HS_CH0_GPIO       (18)
#define LEDC_HS_CH0_CHANNEL    LEDC_CHANNEL_0
#define LEDC_HS_CH1_GPIO       (19)
#define LEDC_HS_CH1_CHANNEL    LEDC_CHANNEL_1
#define LEDC_LS_TIMER          LEDC_TIMER_1
#define LEDC_LS_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_LS_CH2_GPIO       (4)
#define LEDC_LS_CH2_CHANNEL    LEDC_CHANNEL_2
#define LEDC_LS_CH3_GPIO       (5)
#define LEDC_LS_CH3_CHANNEL    LEDC_CHANNEL_3
#define LEDC_LS_CH4_GPIO       (6)
#define LEDC_LS_CH4_CHANNEL    LEDC_CHANNEL_4

#define LEDC_TEST_FADE_TIME    (3000)

#define LEDC_NUM_TEST 3

//bts7960
#define DEADZONE_MOTOR 5
#define MIN_MOTOR_DUTY_FWD 230
#define MAX_MOTOR_DUTY_FWD 1030
#define MIN_MOTOR_DUTY_BWD 230
#define MAX_MOTOR_DUTY_BWD 1030

//mg996r
#define MIN_SERVO_DUTY 230
#define MAX_SERVO_DUTY 1030

static uint8_t current_angle = 0;
static int8_t current_motor = 0;

static SemaphoreHandle_t xMutex = NULL;

static const char *TAG = "led_library";

static int led_state = 0;

/*
* Prepare and set configuration of timers
* that will be used by LED Controller
*/
static ledc_timer_config_t ledc_timer = {
    .duty_resolution = LEDC_TIMER_13_BIT, // resolution of PWM duty
    .freq_hz = 50,                      // frequency of PWM signal
    .speed_mode = LEDC_LS_MODE,           // timer mode
    .timer_num = LEDC_LS_TIMER,            // timer index
    .clk_cfg = LEDC_AUTO_CLK,              // Auto select the source clock
};

/*
* Prepare individual configuration
* for each channel of LED Controller
* by selecting:
* - controller's channel number
* - output duty cycle, set initially to 0
* - GPIO number where LED is connected to
* - speed mode, either high or low
* - timer servicing selected channel
*   Note: if different channels use one timer,
*         then frequency and bit_num of these channels
*         will be the same
*/
static ledc_channel_config_t ledc_channel[LEDC_NUM_TEST] = {

    {
        .channel    = LEDC_LS_CH2_CHANNEL,
        .duty       = 0,
        .gpio_num   = LEDC_LS_CH2_GPIO,
        .speed_mode = LEDC_LS_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_LS_TIMER,
        .flags.output_invert = 0
    },
    {
        .channel    = LEDC_LS_CH3_CHANNEL,
        .duty       = 0,
        .gpio_num   = LEDC_LS_CH3_GPIO,
        .speed_mode = LEDC_LS_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_LS_TIMER,
        .flags.output_invert = 0
    },
    {
        .channel    = LEDC_LS_CH4_CHANNEL,
        .duty       = 0,
        .gpio_num   = LEDC_LS_CH4_GPIO,
        .speed_mode = LEDC_LS_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_LS_TIMER,
        .flags.output_invert = 0
    },
};

static SemaphoreHandle_t counting_sem = NULL;

/*
 * This callback function will be called when fade operation has ended
 * Use callback only if you are aware it is being called inside an ISR
 * Otherwise, you can use a semaphore to unblock tasks
 */
static IRAM_ATTR bool cb_ledc_fade_end_event(const ledc_cb_param_t *param, void *user_arg)
{
    BaseType_t taskAwoken = pdFALSE;

    if (param->event == LEDC_FADE_END_EVT) {
        SemaphoreHandle_t counting_sem = (SemaphoreHandle_t) user_arg;
        xSemaphoreGiveFromISR(counting_sem, &taskAwoken);
    }

    return (taskAwoken == pdTRUE);
}

/**
 * Initalize led
 * -Initialize mutex
 * -Reset pin
 * -Set output mode
 * -Load previous state in nvs and apply it
 */
void led_init() {

    if( xMutex != NULL )
    {
        log_mqtt(LOG_WARN, TAG, true, "Mutex already initialized");
        return;
    }

    xMutex = xSemaphoreCreateMutex();

    if( xMutex == NULL )
    {
        log_mqtt(LOG_ERROR, TAG, true, "Error on mutex creation");
        return;
    }

    //reset gpio pin
    esp_err_t err = gpio_reset_pin(LED_PIN);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) resetting pin %d", esp_err_to_name(err), LED_PIN);
        return;
    }

    //set output mode for gpio
    err = gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting direction pin %d", esp_err_to_name(err), LED_PIN);
        return;
    }

    //load led state in nvs and apply it to gpio
    err = load_nvs_int("led_state", &led_state);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) loading nvs int : led_state", esp_err_to_name(err));
        return;
    }
    err = gpio_set_level(LED_PIN, led_state);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting level on pin : %d", esp_err_to_name(err), LED_PIN);
        return;
    }

    //ESP_LOGI(TAG, "Led initialized on pin %d", LED_PIN);
    log_mqtt(LOG_INFO, TAG, true, "Led initialized on pin %d", LED_PIN);

}

/**
 * Led on : set level of gpio to HIGH and save it to nvs
 */
void led_on() {

    if (xMutex == NULL) {
        log_mqtt(LOG_ERROR, TAG, true, "Error on mutex creation");
        return;
    }

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        if (led_state == 0) {
            led_state = 1;
            esp_err_t err = gpio_set_level(LED_PIN, led_state);
            if (err != ESP_OK) {
                log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting level on pin : %d", esp_err_to_name(err), LED_PIN);
                xSemaphoreGive(xMutex);
                return;
            }
            err = save_nvs_int("led_state", led_state);
            if (err != ESP_OK) {
                log_mqtt(LOG_ERROR, TAG, true, "Error (%s) loading saving led_state in nvs", esp_err_to_name(err));
                xSemaphoreGive(xMutex);
                return;
            }
            //ESP_LOGI(TAG, "Led on");
            log_mqtt(LOG_INFO, TAG, true, "Led on");
        }
        xSemaphoreGive(xMutex);
    }
}

/**
 * Led off : set level of gpio to LOW and save it to nvs
 */
void led_off() {

    if (xMutex == NULL) {
        log_mqtt(LOG_ERROR, TAG, true, "Error on mutex creation");
        return;
    }

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        if (led_state != 0) {
            led_state = 0;
            esp_err_t err = gpio_set_level(LED_PIN, led_state);
            if (err != ESP_OK) {
                log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting level on pin : %d", esp_err_to_name(err), LED_PIN);
                xSemaphoreGive(xMutex);
                return;
            }
            err = save_nvs_int("led_state", led_state);
            if (err != ESP_OK) {
                log_mqtt(LOG_ERROR, TAG, true, "Error (%s) loading saving led_state in nvs", esp_err_to_name(err));
                xSemaphoreGive(xMutex);
                return;
            }
            log_mqtt(LOG_INFO, TAG, true, "Led off");
        }
        xSemaphoreGive(xMutex);
    }
}

/**
 * Led toggle : reverse level and save it to nvs
 */
void led_toggle() {

    if (xMutex == NULL) {
        log_mqtt(LOG_ERROR, TAG, true, "Error on mutex creation");
        return;
    }

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        led_state = !led_state;
        esp_err_t err = gpio_set_level(LED_PIN, led_state);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting level on pin : %d", esp_err_to_name(err), LED_PIN);
            xSemaphoreGive(xMutex);
            return;
        }
        err = save_nvs_int("led_state", led_state);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) loading saving led_state in nvs", esp_err_to_name(err));
            xSemaphoreGive(xMutex);
            return;
        }
        //ESP_LOGI(TAG, "Led toggled to : %d", led_state);
        log_mqtt(LOG_INFO, TAG, true, "Led toggled to : %d", led_state);
        xSemaphoreGive(xMutex);
    }
}

/**
 * Close led
 * -Destroy mutex
 */
void close_led() {

    if (xMutex == NULL) {
        log_mqtt(LOG_ERROR, TAG, true, "Error on mutex creation");
        return;
    }

    vSemaphoreDelete(xMutex);
    xMutex = NULL;

    //ESP_LOGI(TAG, "Led closed");
    log_mqtt(LOG_INFO, TAG, true, "Led closed");
}

/**
 * Get led state
 * @return int current led state, -1 if error
 */
int get_led_state() {
    int led = -1;

    if (xMutex == NULL) {
        log_mqtt(LOG_ERROR, TAG, true, "Error on mutex creation");
        return led;
    }

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        led = led_state;
        xSemaphoreGive(xMutex);
    }
    return led;
}

void init_ledc() {

    esp_err_t err;
    int ch;

    // Set configuration of timer0 for high speed channels
    err = ledc_timer_config(&ledc_timer);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting timer on pin %d", esp_err_to_name(err), LEDC_LS_CH2_GPIO);
        return;
    }

    // Set LED Controller with previously prepared configuration
    for (ch = 0; ch < LEDC_NUM_TEST; ch++) {
        err = ledc_channel_config(&ledc_channel[ch]);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting channel %d",
                esp_err_to_name(err), ch);
        return;
    }
    }
    

    // Initialize fade service.
    err = ledc_fade_func_install(0);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) installing fade", esp_err_to_name(err));
        return;
    }
    ledc_cbs_t callbacks = {
        .fade_cb = cb_ledc_fade_end_event
    };
    counting_sem = xSemaphoreCreateCounting(LEDC_NUM_TEST, 0);
    if (counting_sem == NULL) {
        log_mqtt(LOG_ERROR, TAG, true, "Error creating ledc semaphore");
        return;
    }


    for (ch = 0; ch < LEDC_NUM_TEST; ch++) {
        ledc_cb_register(ledc_channel[ch].speed_mode, ledc_channel[ch].channel, &callbacks, (void *) counting_sem);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) registering callback ch %d", esp_err_to_name(err), ch);
            return;
        }
    }

    log_mqtt(LOG_INFO, TAG, true, "LEDC initialized on pin %d", LEDC_LS_CH2_GPIO);
}

void ledc_duty_fade(const uint32_t duty, const uint8_t idx) {

    esp_err_t err;

    err = ledc_set_fade_with_time(ledc_channel[idx].speed_mode,
                                ledc_channel[idx].channel, duty, LEDC_TEST_FADE_TIME);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting fade duty %d ch %d",
            esp_err_to_name(err), duty, idx);
        return;
    }

    err = ledc_fade_start(ledc_channel[idx].speed_mode,
                    ledc_channel[idx].channel, LEDC_FADE_NO_WAIT);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) starting fade duty %d ch %d",
            esp_err_to_name(err), duty, idx);
        return;
    }

    log_mqtt(LOG_INFO, TAG, true, "Starting fade : %d, on pin %d", duty, idx);
    xSemaphoreTake(counting_sem, portMAX_DELAY);
}

void ledc_duty(const uint32_t duty, const uint8_t idx) {

    esp_err_t err;

    err = ledc_set_duty(ledc_channel[idx].speed_mode, ledc_channel[idx].channel, duty);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting duty %d pin %d",
            esp_err_to_name(err), duty, LEDC_LS_CH2_GPIO);
        return;
    }

    err = ledc_update_duty(ledc_channel[idx].speed_mode, ledc_channel[idx].channel);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) updating duty %d pin %d",
            esp_err_to_name(err), duty, LEDC_LS_CH2_GPIO);
        return;
    }

    log_mqtt(LOG_INFO, TAG, true, "Duty : %d, on pin %d", duty, idx);
}

uint8_t get_servo_angle() {
    return current_angle;
}

void ledc_angle(int16_t angle) {

    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180; 

    if (current_angle != angle) {
        current_angle = angle;
        ledc_duty(
            MIN_SERVO_DUTY + ((MAX_SERVO_DUTY - MIN_SERVO_DUTY) * angle) / 180, DIRECTION_IDX
        );
        log_mqtt(LOG_INFO, TAG, true, "Angle : %d, on pin %d", angle, LEDC_LS_CH2_GPIO);
    }
    
}

void ledc_motor(int16_t motor_percent) {

    if (motor_percent < -100) motor_percent = -100;
    if (motor_percent > 100) motor_percent = 100; 

    if (abs(motor_percent) < DEADZONE_MOTOR) {
        motor_percent = 0;
    }

    if (current_motor != motor_percent) {
        current_motor = motor_percent;

        if (current_motor > 0) {
            ledc_duty(
                MIN_MOTOR_DUTY_FWD + ((MAX_MOTOR_DUTY_FWD - MIN_MOTOR_DUTY_FWD) * current_motor) / 100, MOTOR_IDX_FWD
            );
            ledc_duty(0, MOTOR_IDX_BWD);
        }

        else if (current_motor < 0) {
            ledc_duty(
                MIN_MOTOR_DUTY_BWD + ((MAX_MOTOR_DUTY_BWD - MIN_MOTOR_DUTY_BWD) * -current_motor) / 100, MOTOR_IDX_BWD
            );
            ledc_duty(0, MOTOR_IDX_FWD);
        }

        else {
            ledc_duty(0, MOTOR_IDX_FWD);
            ledc_duty(0, MOTOR_IDX_BWD);
        }
        
        log_mqtt(LOG_INFO, TAG, true, "Motor : %d", current_motor);
    }
    
}

void print_esp_info_ledc() {

    log_mqtt(LOG_INFO, TAG, true, "============= LEDC capabilities =============");

    log_mqtt(LOG_INFO, TAG, true, "LEDC timers        : %d", SOC_LEDC_TIMER_NUM);
    log_mqtt(LOG_INFO, TAG, true, "LEDC channels      : %d", SOC_LEDC_CHANNEL_NUM);
    log_mqtt(LOG_INFO, TAG, true, "Max duty resolution: %d bits", SOC_LEDC_TIMER_BIT_WIDTH);

    #if SOC_LEDC_SUPPORT_HS_MODE
        log_mqtt(LOG_INFO, TAG, true, "High Speed mode    : SUPPORTED");
    #else
        log_mqtt(LOG_INFO, TAG, true, "High Speed mode    : NOT supported (LS only)");
    #endif

    #if SOC_LEDC_SUPPORT_REF_TICK
        log_mqtt(LOG_INFO, TAG, true, "REF_TICK clock     : SUPPORTED (light sleep safe)");
    #else
        log_mqtt(LOG_INFO, TAG, true, "REF_TICK clock     : NOT supported");
    #endif

    #if SOC_LEDC_SUPPORT_APB_CLOCK
        log_mqtt(LOG_INFO, TAG, true, "APB clock (80 MHz) : SUPPORTED");
    #else
        log_mqtt(LOG_INFO, TAG, true, "APB clock (80 MHz) : NOT supported");
    #endif

    log_mqtt(LOG_INFO, TAG, true, "=============================================");

}