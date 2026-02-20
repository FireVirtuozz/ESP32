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

#if USE_LVGL_SCREEN
#include "lcdLib.h"
#endif

#if DEBUG_GPIO || DEBUG_LEDC
#include "soc/soc_caps.h"
#endif

#if DEBUG_LEDC
#include "hal/ledc_types.h"
#include "soc/ledc_struct.h"
#include "soc/clk_tree_defs.h"
#include "esp_clk_tree.h"
#endif

#if DEBUG_GPIO
#include "hal/gpio_types.h"
#include "esp_private/esp_gpio_reserve.h"
#endif

//timer configs

//for h-bridge bts7960
#define BTS_TIMER           LEDC_TIMER_0
#define BTS_SPEED_MODE      LEDC_HIGH_SPEED_MODE
#define BTS_GPIO_FWD        32
#define BTS_CHANNEL_FWD     LEDC_CHANNEL_0
#define BTS_GPIO_BWD        33
#define BTS_CHANNEL_BWD     LEDC_CHANNEL_1
#define BTS_FREQ            20000 //max frequency BTS : 25kHz
#define BTS_RESOLUTION      LEDC_TIMER_11_BIT

//for servo mg996r
#define MG_TIMER            LEDC_TIMER_1
#define MG_SPEED_MODE       LEDC_HIGH_SPEED_MODE
#define MG_GPIO             (4) //parentheses reliable to avoid bugs in calculus
#define MG_CHANNEL          LEDC_CHANNEL_2
#define MG_FREQ             50 //50 Hz for mg996r
#define MG_RESOLUTION       LEDC_TIMER_15_BIT

#define LEDC_TEST_FADE_TIME    (3000)

#define LEDC_NUM_TEST 3

//duty properties

//bts7960
#define DEADZONE_MOTOR 5
#define MIN_MOTOR_DUTY_FWD 0
#define MAX_MOTOR_DUTY_FWD 2047 //max duty : 2^resolution - 1
#define MIN_MOTOR_DUTY_BWD 0
#define MAX_MOTOR_DUTY_BWD 2047

//mg996r
#define MIN_SERVO_DUTY 1638
#define MAX_SERVO_DUTY 3277

//LED pin gpio connected on breadboard
#define LED_PIN 2

//global variables
static uint8_t current_angle = 0;
static int8_t current_motor = 0;
static int led_state = 0; //TODO : convert to uint8

//mutex for changes to global variables
static SemaphoreHandle_t xMutex = NULL;

static const char *TAG = "led_library";

/*
* Prepare and set configuration of timers
* that will be used by LED Controller
*/
static ledc_timer_config_t ledc_timer_bts = {
    .duty_resolution = BTS_RESOLUTION, // resolution of PWM duty : 0..8191
    .freq_hz = BTS_FREQ,                      // frequency of PWM signal : 20ms
    .speed_mode = BTS_SPEED_MODE,           // timer mode
    .timer_num = BTS_TIMER,            // timer index
    .clk_cfg = LEDC_AUTO_CLK,              // Auto select the source clock
};

static ledc_timer_config_t ledc_timer_mg = {
    .duty_resolution = MG_RESOLUTION, // resolution of PWM duty : 0..8191
    .freq_hz = MG_FREQ,                      // frequency of PWM signal : 20ms
    .speed_mode = MG_SPEED_MODE,           // timer mode
    .timer_num = MG_TIMER,            // timer index
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
        .channel    = BTS_CHANNEL_FWD,
        .duty       = 0,
        .gpio_num   = BTS_GPIO_FWD,
        .speed_mode = BTS_SPEED_MODE,
        .hpoint     = 0,
        .timer_sel  = BTS_TIMER,
        .flags.output_invert = 0
    },
    {
        .channel    = BTS_CHANNEL_BWD,
        .duty       = 0,
        .gpio_num   = BTS_GPIO_BWD,
        .speed_mode = BTS_SPEED_MODE,
        .hpoint     = 0,
        .timer_sel  = BTS_TIMER,
        .flags.output_invert = 0
    },
    {
        .channel    = MG_CHANNEL,
        .duty       = 0,
        .gpio_num   = MG_GPIO,
        .speed_mode = MG_SPEED_MODE,
        .hpoint     = 0,
        .timer_sel  = MG_TIMER,
        .flags.output_invert = 0
    },
};

//semaphore for fade events
static SemaphoreHandle_t counting_sem = NULL;

#if DEBUG_LEDC
static char * mode_str(const ledc_mode_t mode) {
    switch (mode)
    {
    case LEDC_HIGH_SPEED_MODE:
        return "High speed";
    case LEDC_LOW_SPEED_MODE:
        return "Low speed";
    case LEDC_SPEED_MODE_MAX:
        return "Max speed";
    default:
        return "Unknown";
    }
}

static char * itr_str(const ledc_intr_type_t type) {
    switch (type)
    {
    case LEDC_INTR_DISABLE:
        return "Disabled";
    case LEDC_INTR_FADE_END:
        return "Enabled";
    case LEDC_INTR_MAX:
        return "Max";
    default:
        return "Unknown";
    }
}

static char * sleep_str(const ledc_sleep_mode_t sleep) {
    switch (sleep)
    {
    case LEDC_SLEEP_MODE_NO_ALIVE_NO_PD:
        return "No alive, no PD";
    case LEDC_SLEEP_MODE_NO_ALIVE_ALLOW_PD:
        return "No alive, allow PD";
    case LEDC_SLEEP_MODE_KEEP_ALIVE:
        return "Keep alive";
    case LEDC_SLEEP_MODE_INVALID:
        return "Invalid";
    default:
        return "Unknown";
    }
}

static char * clock_str(const ledc_clk_cfg_t clock) {
    switch (clock)
    {
    case LEDC_AUTO_CLK: 
        return "AUTO";
    case LEDC_USE_APB_CLK: //80MHz
        return "APB";
    case LEDC_USE_RC_FAST_CLK: //~8MHz (Low speed only)
        return "RC_FAST";
    case LEDC_USE_REF_TICK: //1MHz
        return "REF_TICK";
    default:
        return "Unknown";
    }
}

static char * event_str(const ledc_cb_event_t event) {
    switch (event)
    {
    case LEDC_FADE_END_EVT:
        return "Fade end";
    default:
        return "Unknown";
    }
}

static soc_module_clk_t get_clock_source_by_register(const ledc_timer_config_t timer) {
    uint32_t tick_sel = LEDC.timer_group[timer.speed_mode].timer[timer.timer_num].conf.tick_sel;
    uint32_t slow_clk_sel = LEDC.conf.slow_clk_sel;

    log_mqtt(LOG_INFO, TAG, true, "High speed clock raw : %d", tick_sel);
    log_mqtt(LOG_INFO, TAG, true, "Low speed clock raw : %d", slow_clk_sel);
    
    // 0 est la valeur par défaut sécurisée (souvent REF_TICK ou INVALID selon le contexte)
    soc_module_clk_t clk = (soc_module_clk_t)0;

    if (timer.speed_mode == LEDC_HIGH_SPEED_MODE) {
        // High Speed : 1 = APB, 0 = REF_TICK, for ESPs
        clk = (tick_sel == 1) ? SOC_MOD_CLK_APB : SOC_MOD_CLK_REF_TICK;
    } else {
        // Low Speed : global clock
        if (slow_clk_sel == 0) {
            clk = SOC_MOD_CLK_APB;
        } else if (slow_clk_sel == 1) {
            clk = SOC_MOD_CLK_RC_FAST;
        } else if (slow_clk_sel == 2) {
            clk = SOC_MOD_CLK_REF_TICK;
        } else {
            clk = SOC_MOD_CLK_APB; // Default fallback.. TODO: other clocks
        }
    }
    return clk;
}

static void print_timer_config(const ledc_timer_config_t config) {
    log_mqtt(LOG_INFO, TAG, true, "Timer index : %d", config.timer_num); //max?
    log_mqtt(LOG_INFO, TAG, true, "Duty resolution : %d", config.duty_resolution); //max?
    log_mqtt(LOG_INFO, TAG, true, "Frequency : %d", config.freq_hz);
    log_mqtt(LOG_INFO, TAG, true, "Speed mode : %s", mode_str(config.speed_mode));
    log_mqtt(LOG_INFO, TAG, true, "Clock : %s", clock_str(config.clk_cfg));
    log_mqtt(LOG_INFO, TAG, true, "Deconfigure : %s", config.deconfigure ? "Yes" : "No");
}

static void print_channel_config(const ledc_channel_config_t config) {
    log_mqtt(LOG_INFO, TAG, true, "Channel : %d", config.channel); //max?
    log_mqtt(LOG_INFO, TAG, true, "GPIO num : %d", config.gpio_num);
    log_mqtt(LOG_INFO, TAG, true, "Speed mode : %s", mode_str(config.speed_mode));
    log_mqtt(LOG_INFO, TAG, true, "Interrupt type (deprecated) : %s", itr_str(config.intr_type));
    log_mqtt(LOG_INFO, TAG, true, "Timer index : %d", config.timer_sel); //max?
    log_mqtt(LOG_INFO, TAG, true, "Duty : %d", config.duty);
    log_mqtt(LOG_INFO, TAG, true, "Hpoint : %d", config.hpoint);
    log_mqtt(LOG_INFO, TAG, true, "Sleep mode : %s", sleep_str(config.sleep_mode));
    log_mqtt(LOG_INFO, TAG, true, "Deconfigure : %s", config.deconfigure ? "Yes" : "No");
    log_mqtt(LOG_INFO, TAG, true, "Output invert : %s", config.flags.output_invert ? "Yes" : "No");
}

static void print_callback_param(const ledc_cb_param_t cb) {
    log_mqtt(LOG_INFO, TAG, true, "Channel : %d", cb.channel);
    log_mqtt(LOG_INFO, TAG, true, "Event : %s", event_str(cb.event));
    log_mqtt(LOG_INFO, TAG, true, "Speed mode : %s", mode_str(cb.speed_mode));
    log_mqtt(LOG_INFO, TAG, true, "Duty : %s", cb.duty);
}
#endif

#if DEBUG_GPIO
static char * drive_cap_str(const gpio_drive_cap_t drive) {
    switch (drive)
    {
    case GPIO_DRIVE_CAP_0:
        return "Weak";
    case GPIO_DRIVE_CAP_1:
        return "Stronger"; 
    case GPIO_DRIVE_CAP_2:
        return "Medium";
    case GPIO_DRIVE_CAP_3:
        return "Strongest";
    case GPIO_DRIVE_CAP_MAX:
        return "MAX";
    default:
        return "Unknown";
    }
}

static void print_gpio_config(const gpio_io_config_t config) {
    //current (mA) to change pin level
    log_mqtt(LOG_INFO, TAG, true, "Drive strength : %s", drive_cap_str(config.drv));

    //multiplexer, owner of pin (uart, spi..)
    log_mqtt(LOG_INFO, TAG, true, "IOMUX function : %u", config.fun_sel);
    //GPIO Matrix : component using pin
    log_mqtt(LOG_INFO, TAG, true, "Outputting index : %u", config.sig_out);

    //Pull signal up to 3.3v when nothing is connected (internal resistances)
    log_mqtt(LOG_INFO, TAG, true, "Pull-up enabled : %s", config.pu ? "Yes" : "No");
    //Pull signal down to 0v when nothing is connected (internal resistances)
    log_mqtt(LOG_INFO, TAG, true, "Pull-down enabled : %s", config.pd ? "Yes" : "No");

    //input mode : reading signal
    log_mqtt(LOG_INFO, TAG, true, "Input enabled : %s", config.ie ? "Yes" : "No");
    //output mode : generating signal
    log_mqtt(LOG_INFO, TAG, true, "Output enabled : %s", config.oe ? "Yes" : "No");

    //Software, or Peripheral (Hardware) for complex protocols (I2C/SPI)
    log_mqtt(LOG_INFO, TAG, true, "Output from peripheral enabled : %s", config.oe_ctrl_by_periph ? "Yes" : "No");

    //signal inversed
    log_mqtt(LOG_INFO, TAG, true, "Output inversed enabled : %s", config.oe_inv ? "Yes" : "No");

    //Modes : Push-pull (normal), Open-drain -> pin tends to gnd 0v, for I2C
    log_mqtt(LOG_INFO, TAG, true, "Open-drain enabled : %s", config.od ? "Yes" : "No");

    //define pin's behaviour when sleeping
    log_mqtt(LOG_INFO, TAG, true, "Pin sleep status enabled : %s", config.slp_sel ? "Yes" : "No");
}
#endif

/*
 * This callback function will be called when fade operation has ended
 * Use callback only if you are aware it is being called inside an ISR
 * Otherwise, you can use a semaphore to unblock tasks
 */
static IRAM_ATTR bool cb_ledc_fade_end_event(const ledc_cb_param_t *param, void *user_arg)
{
    BaseType_t taskAwoken = pdFALSE;

#if DEBUG_LEDC
    print_callback_param(*param);
#endif

    //if fade end event
    if (param->event == LEDC_FADE_END_EVT) {
        //getting the semaphore from parameter
        SemaphoreHandle_t counting_sem_local = (SemaphoreHandle_t) user_arg;
        //from ISR because it is an IRAM function (hardware)
        xSemaphoreGiveFromISR(counting_sem_local, &taskAwoken);
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
        log_mqtt(LOG_INFO, TAG, true, "Led toggled to : %d", led_state);
        xSemaphoreGive(xMutex);
    }
}

/**
 * Close led
 * -Stop ledc channels, fade, timers
 * -Destroy mutex
 */
void close_led() {

    if (xMutex == NULL) {
        log_mqtt(LOG_ERROR, TAG, true, "Error on mutex creation");
        return;
    }

    //destroy ledc
    esp_err_t err;
    for (int i = 0; i < LEDC_NUM_TEST; i++) {
        //idle level to 0 = LOW?
        err = ledc_stop(ledc_channel[i].speed_mode, ledc_channel[i].channel, 0);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) on stop ledc channel %d", esp_err_to_name(err), i);
        }
    }
    ledc_fade_func_uninstall();

    err = ledc_timer_rst(ledc_timer_bts.speed_mode, ledc_timer_bts.timer_num);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) on reset ledc timer BTS", esp_err_to_name(err));
    }
    err = ledc_timer_rst(ledc_timer_mg.speed_mode, ledc_timer_mg.timer_num);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) on reset ledc timer MG", esp_err_to_name(err));
    }

    err = ledc_timer_pause(ledc_timer_bts.speed_mode, ledc_timer_bts.timer_num);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) on pause ledc timer BTS", esp_err_to_name(err));
    }
    err = ledc_timer_pause(ledc_timer_mg.speed_mode, ledc_timer_mg.timer_num);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) on pause ledc timer MG", esp_err_to_name(err));
    }

    if (counting_sem != NULL) {
        vSemaphoreDelete(counting_sem);
        counting_sem = NULL;
    }

    vSemaphoreDelete(xMutex);
    xMutex = NULL;

    log_mqtt(LOG_INFO, TAG, true, "Led & Ledc closed");
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

/**
 * Init ledc
 * -Init timers, channels, fade, count semaphore & register fade callback
 */
void init_ledc() {

    esp_err_t err;
    int ch;

    // Set configuration of timer bts & mg for high speed channels
    err = ledc_timer_config(&ledc_timer_bts);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting timer for BTS", esp_err_to_name(err));
        return;
    }

    err = ledc_timer_config(&ledc_timer_mg);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting timer for MG", esp_err_to_name(err));
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
        err = ledc_cb_register(ledc_channel[ch].speed_mode, ledc_channel[ch].channel, &callbacks, (void *) counting_sem);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) registering callback on channel %d", esp_err_to_name(err), ch);
            return;
        }
    }

    log_mqtt(LOG_INFO, TAG, true, "LEDC initialized");

#if DEBUG_LEDC || DEBUG_GPIO
    print_esp_info_ledc();
#endif
}

void init_all_gpios() {

    led_init();
    init_ledc();
}

/**
 * Apply fade duty
 * -Set & start fade, and then the ISR callback will be called
 */
void ledc_duty_fade(const uint32_t duty, const uint8_t idx) {

    esp_err_t err;

    err = ledc_set_fade_with_time(ledc_channel[idx].speed_mode,
                                ledc_channel[idx].channel, duty, LEDC_TEST_FADE_TIME);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting fade duty %d on channel %d",
            esp_err_to_name(err), duty, idx);
        return;
    }

    
    ledc_fade_mode_t fade_mode = LEDC_FADE_NO_WAIT; //non blocking
    /*ledc_fade_mode_t fade_mode = LEDC_FADE_WAIT_DONE; //blocking */
    err = ledc_fade_start(ledc_channel[idx].speed_mode,
                    ledc_channel[idx].channel, fade_mode);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) starting fade duty %d on channel %d",
            esp_err_to_name(err), duty, idx);
        return;
    }

    log_mqtt(LOG_INFO, TAG, true, "Starting fade : %d, on channel %d", duty, idx);
    xSemaphoreTake(counting_sem, portMAX_DELAY);
}

/**
 * Apply duty
 * -Set & update duty
 */
void ledc_duty(const uint32_t duty, const uint8_t idx) {

    esp_err_t err;

    err = ledc_set_duty(ledc_channel[idx].speed_mode, ledc_channel[idx].channel, duty);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting duty %d on channel %d",
            esp_err_to_name(err), duty, idx);
        return;
    }

    err = ledc_update_duty(ledc_channel[idx].speed_mode, ledc_channel[idx].channel);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) updating duty %d on channel %d",
            esp_err_to_name(err), duty, idx);
        return;
    }

    log_mqtt(LOG_DEBUG, TAG, true, "Duty : %d, on channel %d", duty, idx);
}

/**
 * Get current angle of servo
 */
uint8_t get_servo_angle() {
    return current_angle;
}

/**
 * Apply a ledc angle for MG servo
 * -Clamp value, check if the value changed, transform angle to duty & apply it
 * -Write to screen if set
 */
void ledc_angle(int16_t angle) {

    //clamp
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;

    if (current_angle != angle) {
        current_angle = angle;

        //applying duty
        ledc_duty(
            MIN_SERVO_DUTY + ((MAX_SERVO_DUTY - MIN_SERVO_DUTY) * angle) / 180, DIRECTION_IDX
        );

        log_mqtt(LOG_DEBUG, TAG, true, "Angle : %d, on pin %d", angle, MG_GPIO);

#if WRITE_ANGLE_SCREEN

#if USE_LVGL_SCREEN
        set_bar_steer((current_angle - 90) * 200 / 180);
#else
        //print to screen
        char tmp[30];
        snprintf(tmp, sizeof(tmp), "Angle : %d", current_angle);
        ssd1306_draw_string(tmp, 0, 2);
#endif
#endif
    }
    
}

/**
 * Apply a ledc motor for BTS
 * -Clamp value, check if the value changed, transform motor to duty & apply it
 * -Write to screen if set
 */
void ledc_motor(int16_t motor_percent) {

    //clamp
    if (motor_percent < -100) motor_percent = -100;
    if (motor_percent > 100) motor_percent = 100; 

    //deadzone
    if (abs(motor_percent) < DEADZONE_MOTOR) {
        motor_percent = 0;
    }

    if (current_motor != motor_percent) {
        current_motor = motor_percent;

        //applying duty forward et 0 backward
        if (current_motor > 0) {
            ledc_duty(
                MIN_MOTOR_DUTY_FWD + ((MAX_MOTOR_DUTY_FWD - MIN_MOTOR_DUTY_FWD) * current_motor) / 100, MOTOR_IDX_FWD
            );
            ledc_duty(0, MOTOR_IDX_BWD);
        }

        //applying duty backward et 0 forward
        else if (current_motor < 0) {
            ledc_duty(
                MIN_MOTOR_DUTY_BWD + ((MAX_MOTOR_DUTY_BWD - MIN_MOTOR_DUTY_BWD) * - current_motor) / 100, MOTOR_IDX_BWD
            );
            ledc_duty(0, MOTOR_IDX_FWD);
        }

        //applying 0 to backward & forward
        else {
            ledc_duty(0, MOTOR_IDX_FWD);
            ledc_duty(0, MOTOR_IDX_BWD);
        }
        
        log_mqtt(LOG_DEBUG, TAG, true, "Motor : %d, on pins; fwd : %d, bwd : %d",
            current_motor, BTS_GPIO_FWD, BTS_GPIO_BWD);

#if WRITE_MOTOR_SCREEN

#if USE_LVGL_SCREEN
        set_bar_motor(current_motor);
#else
        //print to screen
        char tmp[30];
        snprintf(tmp, sizeof(tmp), "Motor : %d", current_motor);
        ssd1306_draw_string(tmp, 0, 4);
#endif
#endif
    }
    
}

#if DEBUG_LEDC || DEBUG_GPIO
void print_esp_info_ledc() {

    esp_err_t err;

#if DEBUG_LEDC
    log_mqtt(LOG_INFO, TAG, true, "============= LEDC capabilities =============");

    log_mqtt(LOG_INFO, TAG, true, "LEDC timers        : %d", SOC_LEDC_TIMER_NUM);
    log_mqtt(LOG_INFO, TAG, true, "LEDC channels      : %d", SOC_LEDC_CHANNEL_NUM);
    log_mqtt(LOG_INFO, TAG, true, "Max duty resolution: %d bits", SOC_LEDC_TIMER_BIT_WIDTH);

    //SOC_LEDC_HAS_TIMER_SPECIFIC_MUX;

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

    uint32_t val;

    int val_int;
    for (int i = 0; i < LEDC_NUM_TEST; i++) {
        print_channel_config(ledc_channel[i]);
        val_int = ledc_get_hpoint(ledc_channel[i].speed_mode, ledc_channel[i].channel);
        log_mqtt(LOG_INFO, TAG, true, "Hpoint channel %d : %d", i, val_int);
        val = ledc_get_duty(ledc_channel[i].speed_mode, ledc_channel[i].channel);
        log_mqtt(LOG_INFO, TAG, true, "Duty channel %d : %d", i, val);
    }

    print_timer_config(ledc_timer_bts);
    print_timer_config(ledc_timer_mg);

    val = ledc_get_freq(ledc_timer_bts.speed_mode, ledc_timer_bts.timer_num);
    log_mqtt(LOG_INFO, TAG, true, "BTS Frequence : %d", val);

    //clock : APB, REF_TICK, RC_FAST
    soc_module_clk_t clock = get_clock_source_by_register(ledc_timer_bts);
    
    //precision : cached, approx, exact, invalid
    //for specific clocks, like RC_FAST 
    esp_clk_tree_src_freq_precision_t precision = ESP_CLK_TREE_SRC_FREQ_PRECISION_EXACT; //exact
    err = esp_clk_tree_src_get_freq_hz(clock, precision, &val);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) getting clock frequency for BTS",
            esp_err_to_name(err));
    }
    log_mqtt(LOG_INFO, TAG, true, "BTS clock frequency : %d", val);

    val = ledc_find_suitable_duty_resolution(val, BTS_FREQ);
    log_mqtt(LOG_INFO, TAG, true, "BTS suitable duty resolution : %d", val);

    val = ledc_get_freq(ledc_timer_mg.speed_mode, ledc_timer_mg.timer_num);
    log_mqtt(LOG_INFO, TAG, true, "MG Frequence : %d", val);

    clock = get_clock_source_by_register(ledc_timer_mg);

    precision = ESP_CLK_TREE_SRC_FREQ_PRECISION_EXACT; //exact
    err = esp_clk_tree_src_get_freq_hz(clock, precision, &val);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) getting clock frequency for MG",
            esp_err_to_name(err));
    }
    log_mqtt(LOG_INFO, TAG, true, "MG clock frequency : %d", val);

    val = ledc_find_suitable_duty_resolution(val, MG_FREQ);
    log_mqtt(LOG_INFO, TAG, true, "MG suitable duty resolution : %d", val);
#endif

#if DEBUG_GPIO
    log_mqtt(LOG_INFO, TAG, true, "============= GPIO capabilities =============");
    log_mqtt(LOG_INFO, TAG, true, "GPIO pin count        : %d", GPIO_PIN_COUNT);

    gpio_io_config_t out_io_config;
    gpio_drive_cap_t drive_cap;
    for (int i = 0; i < GPIO_PIN_COUNT; i++) {
        err = gpio_get_io_config(i, &out_io_config);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) getting GPIO config for pin %d",
                esp_err_to_name(err), i);
        }
        log_mqtt(LOG_INFO, TAG, true, "GPIO pin %d; valid : %d, valid output: %d, valid pad : %d",
            i, GPIO_IS_VALID_GPIO(i), GPIO_IS_VALID_OUTPUT_GPIO(i), GPIO_IS_VALID_DIGITAL_IO_PAD(i));
        ;
        log_mqtt(LOG_INFO, TAG, true, "Reserved : %s", esp_gpio_is_reserved(BIT64(i)) ? "Yes" : "No");
        print_gpio_config(out_io_config);
        log_mqtt(LOG_INFO, TAG, true, "Level : %d", gpio_get_level(i));
        err = gpio_get_drive_capability(i, &drive_cap);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) getting GPIO drive capability for pin %d",
                esp_err_to_name(err), i);
        }
        log_mqtt(LOG_INFO, TAG, true, "Strength : %s", drive_cap_str(drive_cap));
    }
#endif

    log_mqtt(LOG_INFO, TAG, true, "=============================================");
}
#endif

void dump_gpio_stats() {
#if DEBUG_GPIO
    gpio_dump_io_configuration(stdout, SOC_GPIO_VALID_GPIO_MASK);
#endif
}

/*
==========================
other functions for ledc
==========================
*/

//ledc_set_pin to configure channel config better
//ledc_set_freq

//ledc_set_duty_and_update
//ledc_set_duty_with_hpoint;

//ledc_set_fade
//ledc_set_fade_with_step
//ledc_set_fade_step_and_start
//ledc_set_fade_time_and_start

//ledc_isr_register (deprecated)

//ledc_timer_resume
//ledc_bind_channel_timer (deprecated)

/*
==========================
other functions for gpio
==========================
*/

//TODO : change gpio_reset to gpio_config to control everything

/*for interruptions rising / falling edges, in input mode*/
//gpio_set_intr_type
//gpio_intr_enable
//gpio_intr_disable
//gpio_isr_register
//gpio_install_isr_service
//gpio_uninstall_isr_service
//gpio_isr_handler_add
//gpio_isr_handler_remove

/*for gpio config*/
//gpio_config
//gpio_input_enable
//gpio_set_pull_mode
//gpio_wakeup_enable
//gpio_wakeup_disable
//gpio_pullup_en
//gpio_pullup_dis
//gpio_pulldown_en
//gpio_pulldown_dis
//gpio_output_enable
//gpio_output_disable
//gpio_od_enable
//gpio_od_disable
//gpio_set_drive_capability
//gpio_hold_en
//gpio_hold_dis
//gpio_deep_sleep_hold_en
//gpio_deep_sleep_hold_dis
//gpio_sleep_sel_en
//gpio_sleep_sel_dis
//gpio_sleep_set_direction
//gpio_sleep_set_pull_mode

//rtc gpios : for sleep-active gpios
