#include "debug_helper.h"
#include "actuators_lib.h"
#include "log_lib.h"

static const char *TAG = "debug_helper_library";

#if CONFIG_DEBUG_GPIO || CONFIG_DEBUG_LEDC
#include "soc/soc_caps.h"
#endif

#if CONFIG_DEBUG_LEDC
#include "hal/ledc_types.h"
#include "soc/ledc_struct.h"
#include "soc/clk_tree_defs.h"
#include "esp_clk_tree.h"
#endif

#if CONFIG_DEBUG_GPIO
#include "hal/gpio_types.h"
#include "esp_private/esp_gpio_reserve.h"
#include "driver/gpio.h"
#endif

#if CONFIG_DEBUG_LEDC

static const char *mode_str(ledc_mode_t mode) {
    switch (mode) {
        case LEDC_HIGH_SPEED_MODE: return "High speed";
        case LEDC_LOW_SPEED_MODE:  return "Low speed";
        case LEDC_SPEED_MODE_MAX:  return "Max speed";
        default:                   return "Unknown";
    }
}

static const char *clock_str(ledc_clk_cfg_t clock) {
    switch (clock) {
        case LEDC_AUTO_CLK:       return "AUTO";
        case LEDC_USE_APB_CLK:    return "APB";       // 80MHz
        case LEDC_USE_RC_FAST_CLK: return "RC_FAST";  // ~8MHz, low speed only
        case LEDC_USE_REF_TICK:   return "REF_TICK";  // 1MHz
        default:                  return "Unknown";
    }
}

static soc_module_clk_t get_clock_source_by_register(ledc_timer_config_t timer) {
    uint32_t tick_sel = LEDC.timer_group[timer.speed_mode].timer[timer.timer_num].conf.tick_sel;
    uint32_t slow_clk_sel = LEDC.conf.slow_clk_sel;

    log_msg(TAG, "High speed clock raw: %" PRIu32, tick_sel);
    log_msg(TAG, "Low speed clock raw: %" PRIu32, slow_clk_sel);

    soc_module_clk_t clk = (soc_module_clk_t)0; // safe default (REF_TICK/INVALID depending on context)

    if (timer.speed_mode == LEDC_HIGH_SPEED_MODE) {
        // High speed: 1 = APB, 0 = REF_TICK
        clk = (tick_sel == 1) ? SOC_MOD_CLK_APB : SOC_MOD_CLK_REF_TICK;
    } else {
        switch (slow_clk_sel) {
            case 0: clk = SOC_MOD_CLK_APB; break;
            case 1: clk = SOC_MOD_CLK_RC_FAST; break;
            case 2: clk = SOC_MOD_CLK_REF_TICK; break;
            default: clk = SOC_MOD_CLK_APB; break; // fallback, TODO: cover remaining clocks
        }
    }
    return clk;
}

static void print_timer_config(ledc_timer_config_t config) {
    log_msg(TAG, "Timer index: %d", config.timer_num);
    log_msg(TAG, "Duty resolution: %d", config.duty_resolution);
    log_msg(TAG, "Frequency: %" PRIu32, config.freq_hz);
    log_msg(TAG, "Speed mode: %s", mode_str(config.speed_mode));
    log_msg(TAG, "Clock: %s", clock_str(config.clk_cfg));
    log_msg(TAG, "Deconfigure: %s", config.deconfigure ? "Yes" : "No");
}

/**
 * Print frequency + resolved clock source + suitable duty resolution for a
 * given timer. Small helper shared by every per-actuator diagnostic block below.
 */
static void print_timer_runtime_info(ledc_timer_config_t timer_cfg, uint32_t pwm_freq, const char *label) {
    uint32_t freq = ledc_get_freq(timer_cfg.speed_mode, timer_cfg.timer_num);
    log_msg(TAG, "%s frequency: %" PRIu32, label, freq);

    soc_module_clk_t clock = get_clock_source_by_register(timer_cfg);
    uint32_t clock_freq = 0;
    esp_err_t err = esp_clk_tree_src_get_freq_hz(clock, ESP_CLK_TREE_SRC_FREQ_PRECISION_EXACT, &clock_freq);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) getting clock frequency for %s", esp_err_to_name(err), label);
        return;
    }
    log_msg(TAG, "%s clock frequency: %" PRIu32, label, clock_freq);

    uint32_t suitable_res = ledc_find_suitable_duty_resolution(clock_freq, pwm_freq);
    log_msg(TAG, "%s suitable duty resolution: %" PRIu32, label, suitable_res);
}

#endif // CONFIG_DEBUG_LEDC

#if CONFIG_DEBUG_GPIO

static const char *drive_cap_str(gpio_drive_cap_t drive) {
    switch (drive) {
        case GPIO_DRIVE_CAP_0:   return "Weak";
        case GPIO_DRIVE_CAP_1:   return "Stronger";
        case GPIO_DRIVE_CAP_2:   return "Medium";
        case GPIO_DRIVE_CAP_3:   return "Strongest";
        case GPIO_DRIVE_CAP_MAX: return "MAX";
        default:                 return "Unknown";
    }
}

static void print_gpio_config(gpio_io_config_t config) {
    log_msg(TAG, "Drive strength: %s", drive_cap_str(config.drv));
    log_msg(TAG, "IOMUX function: %u", config.fun_sel);
    log_msg(TAG, "Outputting index: %u", config.sig_out);
    log_msg(TAG, "Pull-up enabled: %s", config.pu ? "Yes" : "No");
    log_msg(TAG, "Pull-down enabled: %s", config.pd ? "Yes" : "No");
    log_msg(TAG, "Input enabled: %s", config.ie ? "Yes" : "No");
    log_msg(TAG, "Output enabled: %s", config.oe ? "Yes" : "No");
    log_msg(TAG, "Output from peripheral enabled: %s", config.oe_ctrl_by_periph ? "Yes" : "No");
    log_msg(TAG, "Output inverted enabled: %s", config.oe_inv ? "Yes" : "No");
    log_msg(TAG, "Open-drain enabled: %s", config.od ? "Yes" : "No");
    log_msg(TAG, "Pin sleep status enabled: %s", config.slp_sel ? "Yes" : "No");
}

#endif // CONFIG_DEBUG_GPIO

esp_err_t print_esp_info_ledc(void) {
#if CONFIG_DEBUG_LEDC
    log_msg(TAG, "============= LEDC capabilities =============");
    log_msg(TAG, "LEDC timers: %d", SOC_LEDC_TIMER_NUM);
    log_msg(TAG, "LEDC channels: %d", SOC_LEDC_CHANNEL_NUM);
    log_msg(TAG, "Max duty resolution: %d bits", SOC_LEDC_TIMER_BIT_WIDTH);

#if SOC_LEDC_SUPPORT_HS_MODE
    log_msg(TAG, "High Speed mode: SUPPORTED");
#else
    log_msg(TAG, "High Speed mode: NOT supported (LS only)");
#endif

#if SOC_LEDC_SUPPORT_REF_TICK
    log_msg(TAG, "REF_TICK clock: SUPPORTED (light sleep safe)");
#else
    log_msg(TAG, "REF_TICK clock: NOT supported");
#endif

#if SOC_LEDC_SUPPORT_APB_CLOCK
    log_msg(TAG, "APB clock (80MHz): SUPPORTED");
#else
    log_msg(TAG, "APB clock (80MHz): NOT supported");
#endif

    // Each block below is guarded both by CONFIG_DEBUG_LEDC (checked above)
    // and by the actuator's own CONFIG_USE_xxx: printing a timer that was
    // never compiled in would reference an undefined extern and fail to link.
#if CONFIG_USE_BTS7960
    print_timer_config(ledc_timer_bts);
    print_timer_runtime_info(ledc_timer_bts, BTS_FREQ, "BTS7960");
#endif

#if CONFIG_USE_MG996R
    print_timer_config(ledc_timer_mg);
    print_timer_runtime_info(ledc_timer_mg, MG_FREQ, "MG996R");
#endif

#if CONFIG_USE_KY006
    print_timer_config(ledc_timer_buzzer);
    print_timer_runtime_info(ledc_timer_buzzer, BUZZER_START_FREQ, "Buzzer");
#endif

#if CONFIG_USE_KY029
    print_timer_config(ledc_timer_ky029);
    print_timer_runtime_info(ledc_timer_ky029, KY029_FREQ, "KY029");
#endif

#if CONFIG_USE_KY009
    print_timer_config(ledc_timer_ky009);
    print_timer_runtime_info(ledc_timer_ky009, KY009_FREQ, "KY009");
#endif

#endif // CONFIG_DEBUG_LEDC

#if CONFIG_DEBUG_GPIO
    log_msg(TAG, "============= GPIO capabilities =============");
    log_msg(TAG, "GPIO pin count: %d", GPIO_PIN_COUNT);

    for (int i = 0; i < GPIO_PIN_COUNT; i++) {
        gpio_io_config_t out_io_config;
        esp_err_t err = gpio_get_io_config(i, &out_io_config);
        if (err != ESP_OK) {
            log_msg(TAG, "Error (%s) getting GPIO config for pin %d", esp_err_to_name(err), i);
            continue;
        }

        log_msg(TAG, "GPIO pin %d; valid: %d, valid output: %d, valid pad: %d",
            i, GPIO_IS_VALID_GPIO(i), GPIO_IS_VALID_OUTPUT_GPIO(i), GPIO_IS_VALID_DIGITAL_IO_PAD(i));
        log_msg(TAG, "Reserved: %s", esp_gpio_is_reserved(BIT64(i)) ? "Yes" : "No");
        print_gpio_config(out_io_config);
        log_msg(TAG, "Level: %d", gpio_get_level(i));

        gpio_drive_cap_t drive_cap;
        err = gpio_get_drive_capability(i, &drive_cap);
        if (err != ESP_OK) {
            log_msg(TAG, "Error (%s) getting GPIO drive capability for pin %d", esp_err_to_name(err), i);
            continue;
        }
        log_msg(TAG, "Strength: %s", drive_cap_str(drive_cap));
    }
#endif // CONFIG_DEBUG_GPIO

    log_msg(TAG, "=============================================");
    return ESP_OK;
}

esp_err_t dump_gpio_stats(void) {
#if CONFIG_DEBUG_GPIO
    gpio_dump_io_configuration(stdout, SOC_GPIO_VALID_GPIO_MASK);
#endif
    return ESP_OK;
}
