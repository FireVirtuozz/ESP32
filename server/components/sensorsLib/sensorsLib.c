#include "sensorsLib.h"
#include "driver/gpio.h"
#include "logLib.h"

#include "esp_rom_sys.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/i2c_master.h"

#include "driver/spi_common.h"
#include "driver/spi_master.h"

#include "driver/pulse_cnt.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_continuous.h"
#include "soc/soc_caps.h"

#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#if CONFIG_USE_UDPLIB
#include "udpLib.h"
#endif

#if CONFIG_USE_ESPNOW
#include "espnowLib.h"
#endif

#if CONFIG_USE_LEDLIB
#include "ledLib.h"
#include "cmdLib.h"
#endif

static const char * TAG = "sensors_library";

static volatile bool monitoring = false;

#if CONFIG_USE_INA226 || CONFIG_USE_MPU9250 || CONFIG_USE_VL53L1X || CONFIG_USE_AS5600

#define SCL_GPIO 4
#define SDA_GPIO 5

//data transferred in MSB

i2c_master_bus_config_t master_cfg = {
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .i2c_port = I2C_NUM_0,
    .scl_io_num = SCL_GPIO,
    .sda_io_num = SDA_GPIO,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
};
i2c_master_bus_handle_t master_handle = NULL;
#endif

void serialize_header(header_sensor_t *hd, uint8_t *buf) {
    if (hd == NULL || buf == NULL) {
        return;
    }
    buf[0] = hd->type;
    buf[1] = hd->esp_id;
    memcpy(&buf[2], &hd->timestamp, sizeof(uint32_t)); //little-endian
}

#if CONFIG_USE_HCSR04

#define PULSE_TRIG_DURATION 10 //10us
#define ECHO_TIMEOUT 60 //60ms
#define HC_PERIOD 50

typedef struct hc_cfg_st {
    uint8_t trig_pin;
    uint8_t echo_pin;
    uint8_t hc_id;
    SemaphoreHandle_t sem_hcsr;
    int64_t last_timestamp;
    int64_t echo_duration; 
} hc_cfg_t;

static hc_cfg_t hc_cfgs[2] = {
    {
        .trig_pin = 17,
        .echo_pin = 18,
        .hc_id = 0,
        .echo_duration = 0,
        .last_timestamp = -1,
    },
    {
        .trig_pin = 15,
        .echo_pin = 16,
        .hc_id = 1,
        .echo_duration = 0,
        .last_timestamp = -1,
    },
};

static void IRAM_ATTR echo_isr_handler(void* arg)
{
    BaseType_t taskAwoken = pdFALSE;
    hc_cfg_t* hc_cfg = (hc_cfg_t *)arg;

    if (gpio_get_level(hc_cfg->echo_pin) == 1) {
        hc_cfg->last_timestamp = esp_timer_get_time();
    } else { //falling edge
        if (hc_cfg->last_timestamp > 0) {
            hc_cfg->echo_duration = esp_timer_get_time() - hc_cfg->last_timestamp;
            xSemaphoreGiveFromISR(hc_cfg->sem_hcsr, &taskAwoken);
            portYIELD_FROM_ISR(taskAwoken); //wake up the task waiting for semaphore (trig_echo)
        }
        hc_cfg->last_timestamp = -1;  // reset
    }
}

static void print_hc(int64_t val) {
    log_msg(TAG, "value received: %lld, to centimeters: %.1fcm",
            val, val / 58.0);
}

static esp_err_t init_hc_gpio(hc_cfg_t* hc_cfg) {
    esp_err_t err;

    //avoid re-initialization
    if (hc_cfg->sem_hcsr != NULL) {
        log_msg(TAG, "HC-SR04 already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    hc_cfg->sem_hcsr = xSemaphoreCreateBinary();
    if (hc_cfg->sem_hcsr == NULL) {
        log_msg(TAG, "Error creating semaphore");
        return ESP_ERR_NOT_ALLOWED;
    }
    
    err = gpio_reset_pin(hc_cfg->trig_pin);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), hc_cfg->trig_pin);
        return err;
    }
    err = gpio_reset_pin(hc_cfg->echo_pin);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), hc_cfg->echo_pin);
        return err;
    }

    err = gpio_set_direction(hc_cfg->trig_pin, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction pin %d", esp_err_to_name(err), hc_cfg->trig_pin);
        return err;
    }
    err = gpio_set_direction(hc_cfg->echo_pin, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction pin %d", esp_err_to_name(err), hc_cfg->echo_pin);
        return err;
    }

    err = gpio_set_intr_type(hc_cfg->echo_pin, GPIO_INTR_ANYEDGE);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting interrupt type pin %d", esp_err_to_name(err), hc_cfg->echo_pin);
        return err;
    }

    return ESP_OK;
}

static esp_err_t init_hcsr() {
    esp_err_t err;

    for (int i = 0; i < sizeof(hc_cfgs) / sizeof(hc_cfg_t); i++) {
        err = init_hc_gpio(&hc_cfgs[i]);
        if (err != ESP_OK) return err;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        log_msg(TAG, "Error (%s) installing isr", esp_err_to_name(err));
        return err;
    }

    for (int i = 0; i < sizeof(hc_cfgs) / sizeof(hc_cfg_t); i++) {
        err = gpio_isr_handler_add(hc_cfgs[i].echo_pin, echo_isr_handler, &hc_cfgs[i]);
        if (err != ESP_OK) {
            log_msg(TAG, "Error (%s) adding isr handler pin %d", esp_err_to_name(err), hc_cfgs[i].echo_pin);
            return err;
        }
    }

    //or gpio_isr_register()
    log_msg(TAG, "HC-SR04 initialized");
    return ESP_OK;
    
}

static int64_t trigger_echo(hc_cfg_t* hc_cfg) {
    esp_err_t err;

    if (hc_cfg == NULL) return -1;
    if (hc_cfg->sem_hcsr == NULL) return -1;

    //log_msg(TAG, "Triggering echo..");

    err = gpio_set_level(hc_cfg->trig_pin, 1);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting level on pin : %d", esp_err_to_name(err), hc_cfg->trig_pin);
        return -1;
    }
    esp_rom_delay_us(PULSE_TRIG_DURATION);
    err = gpio_set_level(hc_cfg->trig_pin, 0);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting level on pin : %d", esp_err_to_name(err), hc_cfg->trig_pin);
        return -1;
    }

    //log_msg(TAG, "Echo triggered");

    if (xSemaphoreTake(hc_cfg->sem_hcsr, pdMS_TO_TICKS(ECHO_TIMEOUT)) == pdFALSE) {
        return -1; //after tiemout
    } else {
        return hc_cfg->echo_duration; //semaphore given from ISR
    }
}

#define HC_PAYLOAD_SIZE sizeof(int64_t)

/**
 * 50 ms monitoring HC task
 */
static void hc_task(void * params) {
    esp_err_t err;

    if (params == NULL) {
        vTaskDelete(NULL);
        return;
    }
    hc_cfg_t* hc_cfg = (hc_cfg_t *)params;
    
    int64_t val_hc;

    while (monitoring)
    {
        val_hc = trigger_echo(hc_cfg); //blocking

        if (val_hc >= 0 && val_hc < 30000) {
    
            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_HCSR04;
            uint8_t buf[HEADER_SENSOR_SIZE + HC_PAYLOAD_SIZE + 1];
            serialize_header(&header, buf);
            buf[HEADER_SENSOR_SIZE] = hc_cfg->hc_id;
            memcpy(&buf[HEADER_SENSOR_SIZE + 1], &val_hc, sizeof(int64_t));
            //print_hc(val_hc);
            
        #if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
        #endif
        #if CONFIG_USE_ESPNOW
        #endif
        }

        vTaskDelay(pdMS_TO_TICKS(HC_PERIOD));
    }
    vTaskDelete(NULL);
}

#endif

#if CONFIG_USE_INA226

#define INA_ADDR 0x40
#define INA_PERIOD 70

//pointer towards i2c device
i2c_master_dev_handle_t ina_handle;

//describe ina i2c device
i2c_device_config_t ina_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7, //length of i2c address
    .device_address = INA_ADDR, //address of device
    .scl_speed_hz = 400000, //i2c bus frequency (400khz supported by ina)
};

static void print_ina(ina_info_t *ina) {
    log_msg(TAG, "Shunt received: %d, value: %.3fmV", ina->shunt, ina->shunt * 2.5e-6f * 1e3f);
    log_msg(TAG, "Bus received: %d, value: %.3fV", ina->bus, ina->bus * 1.25e-3f);
    log_msg(TAG, "Power received: %d, value: %.3fmW", ina->power, ina->power * 25e-6f * 25 * 1e3f);
    log_msg(TAG, "Current received: %d, value: %.3fmA", ina->current, ina->current * 25e-6f * 1e3f);
}

esp_err_t init_ina() {

    esp_err_t err;

    //config of i2c registers, gpio, start
    if (master_handle == NULL) {
        err = i2c_new_master_bus(&master_cfg, &master_handle);
        if (err != ESP_OK) {
            log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) allocating I²C master bus", esp_err_to_name(err));
            return err;
        }
    }

    err = i2c_master_bus_add_device(master_handle, &ina_cfg, &ina_handle);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) adding INA device to master bus", esp_err_to_name(err));
        return err;
    }

    //write config (ack each frame)
    //start master
    //frame 1 : slave adress byte
    //frame 2 : register pointer
    //frame 3 : data MSB
    //frame 4 : data LSB
    //stop master

    //data frame of 2 bytes
    //bit 15 : reset to default
    //9-11 : AVG
    //6-8 : Vbus CT
    //3-5 : Vsh CT
    //0-2 : Mode
    //default : 01000001 00100111
    uint16_t cfg_frame = 0;
    cfg_frame |= 1 << 15; //reset bit

    uint8_t buf_cfg[3] = {
        0x00,                    //config register
        (cfg_frame >> 8) & 0xFF, //MSB
        cfg_frame & 0xFF         //LSB
    };
    err = i2c_master_transmit(ina_handle, buf_cfg, 3, -1);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) transmitting INA config frame", esp_err_to_name(err));
        return err;
    }

    //wait 2ms for reset
    vTaskDelay(pdMS_TO_TICKS(2));

    //calibration: CAL = 0.00512 / (Current_LSB * Rshunt)
    // Current_LSB = MaxCurrent / 2^15
    // --> I = U/R = 0.08192 / 0.1
    // --> Current_LSB = 0.000025
    // --> CAL = 2048
    uint16_t cal_frame = 2048;

    uint8_t buf_cal[3] = {
        0x05,                    //config register
        (cal_frame >> 8) & 0xFF, //MSB
        cal_frame & 0xFF         //LSB
    };
    err = i2c_master_transmit(ina_handle, buf_cal, 3, -1);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) transmitting INA calibration frame", esp_err_to_name(err));
        return err;
    }

    log_msg(TAG, "INA226 initialized");

    return ESP_OK;
    
}

esp_err_t get_ina_info(ina_info_t *ina_info) {
    if (ina_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err;

    //reading (ack each frame)
    //start master
    //frame 1 : slave adress byte
    //frame 3 : data MSB
    //frame 4 : data LSB
    //stop master

    //change register pointer, write new register to read
    uint8_t reg_ptr[1] = {0x01};
    uint8_t buf_rec[2];

    //shunt voltage
    err = i2c_master_transmit_receive(ina_handle, reg_ptr, 1,
        buf_rec, sizeof(buf_rec), -1);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) receiving shunt", esp_err_to_name(err));
        return err;
    }
    ina_info->shunt = ((uint16_t)buf_rec[0] << 8) | buf_rec[1];

    //bus voltage
    reg_ptr[0] = 0x02;
    err = i2c_master_transmit_receive(ina_handle, reg_ptr, 1,
        buf_rec, sizeof(buf_rec), -1);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) receiving bus", esp_err_to_name(err));
        return err;
    }
    ina_info->bus = ((uint16_t)buf_rec[0] << 8) | buf_rec[1];

    //power
    reg_ptr[0] = 0x03;
    err = i2c_master_transmit_receive(ina_handle, reg_ptr, 1,
        buf_rec, sizeof(buf_rec), -1);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) receiving power", esp_err_to_name(err));
        return err;
    }
    ina_info->power = ((uint16_t)buf_rec[0] << 8) | buf_rec[1];

    //current
    reg_ptr[0] = 0x04;
    err = i2c_master_transmit_receive(ina_handle, reg_ptr, 1,
        buf_rec, sizeof(buf_rec), -1);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) receiving current", esp_err_to_name(err));
        return err;
    }
    ina_info->current = ((uint16_t)buf_rec[0] << 8) | buf_rec[1];

    return ESP_OK;
}

#define INA_PAYLOAD_SIZE (2 * sizeof(int16_t) + 2 * sizeof(uint16_t))

static void serialize_ina(ina_info_t *ina, uint8_t *buf) {
    uint16_t len = HEADER_SENSOR_SIZE;
    memcpy(&buf[len], &ina->bus, sizeof(int16_t));
    len += sizeof(int16_t);
    memcpy(&buf[len], &ina->current, sizeof(uint16_t));
    len += sizeof(uint16_t);
    memcpy(&buf[len], &ina->power, sizeof(uint16_t));
    len += sizeof(uint16_t);
    memcpy(&buf[len], &ina->shunt, sizeof(int16_t));
}

/**
 * Periodic INA monitoring task
 */
static void ina_task(void * params) {
    esp_err_t err;
    ina_info_t ina_info;

    err = init_ina();
    if (err != ESP_OK){
        vTaskDelete(NULL);
        return;
    }

    while (monitoring)
    {
        err = get_ina_info(&ina_info);

        if (err == ESP_OK) {
            
            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_INA226;
            uint8_t buf[HEADER_SENSOR_SIZE + INA_PAYLOAD_SIZE];
            serialize_header(&header, buf);
            serialize_ina(&ina_info, buf);
            //print_ina(&ina_info);
            
        #if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
        #endif
        #if CONFIG_USE_ESPNOW
        #endif
        }

        vTaskDelay(pdMS_TO_TICKS(INA_PERIOD));
    }
    vTaskDelete(NULL);
}

#endif

#if CONFIG_USE_KY035

#define KY_GPIO 34
#define KY_PERIOD 10

// Variables globales pour le driver
static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t adc1_cali_handle = NULL;

// Choisis ton canal (ADC1_CHANNEL_6 est sur le GPIO 34)
#define HALL_ADC_CHANNEL ADC1_CHANNEL_6

#define KY_PAYLOAD_SIZE (sizeof(uint64_t) + sizeof(int64_t))

static volatile ky_info_t ky_info = {0};

static void serialize_ky(ky_info_t *ky, uint8_t *buf) {
    uint16_t len = HEADER_SENSOR_SIZE;
    memcpy(&buf[len], &ky->signal_count, sizeof(uint64_t));
    len += sizeof(uint64_t);
    memcpy(&buf[len], &ky->signal_duration, sizeof(int64_t));
}

static void print_ky(ky_info_t *ky) {
    if (ky->signal_duration == 0) {
        log_msg(TAG, "[mean]; tr/s : 0.000, v: 0.000km/h (Stopped), count: %d",
            ky->signal_count);
        return;
    }

    // On utilise ky->signal_duration qui est déjà la moyenne des deltas de la fenêtre
    double duration_s = ky->signal_duration * 1e-6;
    double tr_per_s = 1.0 / duration_s;
    double v_ms = 3.14159 * 6.75e-2 * tr_per_s;
    double v_kmh = v_ms * 3.6;

    log_msg(TAG, "[mean]; tr/s : %.3f, v: %.3fm/s, v: %.3fkm/h, count: %d",
        tr_per_s, v_ms, v_kmh, ky->signal_count);
}

/**
 * Initialize KY
 */
esp_err_t init_ky() {

    esp_err_t err;

    ky_info.signal_count = 0;
    ky_info.signal_duration = 0;

    // 1. Init unité ADC1
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    err = adc_oneshot_new_unit(&init_config, &adc1_handle);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
        return err;
    }

    // 2. Config canal (GPIO34 = ADC1_CHANNEL_6 sur ESP32)
    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,   // 0~3.1V
    };
    err = adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_6, &chan_config);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(err));
        return err;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_config, &adc1_cali_handle);
#else
    // 3. Calibration (line fitting, ESP32 classique)
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_line_fitting(&cali_config, &adc1_cali_handle);
#endif

    if (err != ESP_OK) {
        // Pas bloquant, on continue sans calibration
        log_msg_lvl(ESP_LOG_WARN, TAG, "Calibration non disponible: %s", esp_err_to_name(err));
        adc1_cali_handle = NULL;
    }

    log_msg(TAG, "KY-003 initialized");

    return ESP_OK;
}

/**
 * Get signal of KY-003.
 * This function should be polled.
 * @returns KY info structure, or NULL if error
 */
ky_info_t* get_signal_info() {

    static int64_t last_timestamp = 0;
    static uint8_t last_lvl = 1;

    int64_t now = esp_timer_get_time();

    uint8_t lvl = gpio_get_level(KY_GPIO);

    if (lvl == 1 && last_lvl == 0) {
        ky_info.signal_count++;
        ky_info.signal_duration = now - last_timestamp;
        last_timestamp = now;
        log_msg(TAG, "[instant]; tr/s : %.3f, v: %.3fm/s, v: %.3fkm/h", 
            1.0 / (ky_info.signal_duration * 1e-6),
            3.14 * 6.75e-2 / (ky_info.signal_duration * 1e-6),
            3.6 * 3.14 * 6.75e-2 / (ky_info.signal_duration * 1e-6));
        return (ky_info_t*)&ky_info;
    }
    
    last_lvl = lvl;

    return NULL;
}

/**
 * Periodic KY monitoring task
 */
static void ky_task(void * params) {

    int raw_val;
    int64_t last_timestamp = 0;
    bool last_state = false;
    const TickType_t poll_delay = pdMS_TO_TICKS(10);

    while (monitoring) {
        esp_err_t err = adc_oneshot_read(adc1_handle, ADC_CHANNEL_6, &raw_val);
        if (err != ESP_OK) {
            vTaskDelay(poll_delay);
            continue;
        }

        bool current_state = (raw_val < 1800);
        //log_msg(TAG, "voltage: %dmV", raw_val);

        if (current_state && !last_state) {
            int64_t now = esp_timer_get_time();
            if (last_timestamp != 0) {
                ky_info_t snapshot = {
                    .signal_count    = ++ky_info.signal_count,
                    .signal_duration = now - last_timestamp,
                };
                ky_info.signal_duration = snapshot.signal_duration;
                
                header_sensor_t header = {0};
                header.esp_id = (uint8_t)CONFIG_ESP_ID;
                header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
                header.type = SENSOR_TYPE_KY035;
                uint8_t buf[HEADER_SENSOR_SIZE + KY_PAYLOAD_SIZE];
                serialize_header(&header, buf);
                serialize_ky(&snapshot, buf);
                
            #if CONFIG_USE_UDPLIB
                send_udp_sensor(buf, sizeof(buf));
            #endif
            #if CONFIG_USE_ESPNOW
            #endif

            }
            last_timestamp = now;
        }

        last_state = current_state;
        vTaskDelay(poll_delay);
    }
    vTaskDelete(NULL);
}

//signal of KY-003 is analogic, so edge interruptions don't work.
//even here, in the function we could have an invalid value of gpio level
//especially with fast polling
//this leads to an invalid count

//another way is by using ADC with one shot read
//and according to voltage, decide if the magnet should be considered or not
//this would correct the error of invalid count

//still same issue, and if period < 10ms, the ADC oneshot generates an error
//due to mutex timeout for adc

#endif

#if CONFIG_USE_MPU9250

#define AK8963_ADDR 0x0C
#define BMP_ADDR 0x76
#define MPU_ADDR 0b1101000
#define MPU_PERIOD 50

//pointer towards i2c device
i2c_master_dev_handle_t mpu_handle;

//describe ina i2c device
i2c_device_config_t mpu_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7, //length of i2c address
    .device_address = MPU_ADDR, //address of device
    .scl_speed_hz = 400000, //i2c bus frequency
};

//pointer towards i2c device
i2c_master_dev_handle_t bmp_handle;

//describe ina i2c device
i2c_device_config_t bmp_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7, //length of i2c address
    .device_address = BMP_ADDR, //address of device
    .scl_speed_hz = 400000, //i2c bus frequency
};

//structure to store pressure calibration values from BMP
typedef struct bmp_dig_p_st {
    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;
} bmp_dig_p_t;

//structure to store temperature calibration values from BMP
typedef struct bmp_dig_t_st {
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;
} bmp_dig_t_t;

static bmp_dig_p_t cal_dig_p;
static bmp_dig_t_t cal_dig_t;

//calibration value to determine pressure using temperature
static int32_t t_fine;

static void print_mpu(mpu_info_t *mpu) {
    log_msg(TAG, "Accel_X received: %d", mpu->accel_x);
    log_msg(TAG, "Accel_Y received: %d", mpu->accel_y);
    log_msg(TAG, "Accel_Z received: %d", mpu->accel_z);
    log_msg(TAG, "Gyro_X received: %d", mpu->gyro_x);
    log_msg(TAG, "Gyro_Y received: %d", mpu->gyro_y);
    log_msg(TAG, "Gyro_Z received: %d", mpu->gyro_z);
    log_msg(TAG, "Temperature MPU received: %d, value: %.3f°C",
        mpu->temp_mpu,(mpu->temp_mpu - 0.0)/333.87 + 21.0);
    log_msg(TAG, "Temperature BMP received: %d, value: %.2f°C", mpu->temp_bmp,
        mpu->temp_bmp / 100.0);
    log_msg(TAG, "Pressure received: %d, value: %.3fbar", mpu->pressure, mpu->pressure * 1e-5);
}

/**
 * Initialize MPU9250 sensor i2c
 */
esp_err_t init_mpu() {
    esp_err_t err;

    //config of i2c registers, gpio, start
    if (master_handle == NULL) {
        err = i2c_new_master_bus(&master_cfg, &master_handle);
        if (err != ESP_OK) {
            log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) allocating I²C master bus", esp_err_to_name(err));
            return err;
        }
    }

    err = i2c_master_bus_add_device(master_handle, &mpu_cfg, &mpu_handle);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) adding mpu device to master bus", esp_err_to_name(err));
        return err;
    }

    err = i2c_master_bus_add_device(master_handle, &bmp_cfg, &bmp_handle);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) adding bmp device to master bus", esp_err_to_name(err));
        return err;
    }

    //reboot mpu
    uint8_t pwr_data[] = {0x6B, 0x00};

    pwr_data[1] |= 1 << 7; //reset

    err = i2c_master_transmit(mpu_handle, pwr_data, sizeof(pwr_data), -1);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) waking up mpu", esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(100)); //wait for reboot

    //mpu device identifier
    uint8_t reg = 0x75;
    uint8_t who;
    err = i2c_master_transmit_receive(mpu_handle, &reg, 1, &who, 1, -1);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) getting who am i mpu", esp_err_to_name(err));
        return err;
    }
    log_msg(TAG, "WHO_AM_I mpu: 0x%02X", who);

    //reboot bmp
    uint8_t reset_bmp_data[] = {0xE0, 0xB6};

    err = i2c_master_transmit(bmp_handle, reset_bmp_data, sizeof(reset_bmp_data), -1);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) resetting bmp", esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(10)); //wait for reboot

    //setup bmp
    uint8_t pwr_bmp_data[] = {0xF4, 0x00};
    //normal mode
    pwr_bmp_data[1] |= 1 << 0;
    pwr_bmp_data[1] |= 1 << 1;

    //measure p : ultra low power, 16bit/2.26Pa resolution
    pwr_bmp_data[1] |= 1 << 2;

    //measure T : ultra low power, 16bit/0.005°C resolution
    pwr_bmp_data[1] |= 1 << 5;

    //see in 3.4 - filter selection (doc) for example applications config

    err = i2c_master_transmit(bmp_handle, pwr_bmp_data, sizeof(pwr_bmp_data), -1);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) waking up bmp", esp_err_to_name(err));
        return err;
    }

    // CONFIG (0x1A) - DLPF_CFG=3 -> gyro filtré à 41Hz (bande passante), delay 5.9ms
    uint8_t config_data[] = {0x1A, 0x03};
    err = i2c_master_transmit(mpu_handle, config_data, sizeof(config_data), -1);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) setting gyro DLPF", esp_err_to_name(err));
        return err;
    }

    // ACCEL_CONFIG_2 (0x1D) - A_DLPFCFG=3, accel_fchoice_b=0 -> accel filtré à 44.8Hz
    uint8_t accel_cfg2_data[] = {0x1D, 0x03};
    err = i2c_master_transmit(mpu_handle, accel_cfg2_data, sizeof(accel_cfg2_data), -1);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) setting accel DLPF", esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(100)); //wait for config setup

    //device identifier
    uint8_t reg_bmp = 0xD0;
    uint8_t who_bmp;
    err = i2c_master_transmit_receive(bmp_handle, &reg_bmp, 1, &who_bmp, 1, -1);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) getting who am i bmp", esp_err_to_name(err));
        return err;
    }
    log_msg(TAG, "WHO_AM_I bmp: 0x%02X", who_bmp);

    //calibration temperature
    uint8_t reg_dig_t = 0x88;
    uint8_t buf_dig_t[6];
    err = i2c_master_transmit_receive(bmp_handle, &reg_dig_t, 1, buf_dig_t, sizeof(buf_dig_t), -1);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) receiving mpu info", esp_err_to_name(err));
        return err;
    }
    
    cal_dig_t.dig_T1 = buf_dig_t[0] | (buf_dig_t[1] << 8);
    cal_dig_t.dig_T2 = (int16_t)(buf_dig_t[2] | (buf_dig_t[3] << 8));
    cal_dig_t.dig_T3 = (int16_t)(buf_dig_t[4] | (buf_dig_t[5] << 8));

    //calibration pressure
    uint8_t reg_dig_p = 0x8E;
    uint8_t buf_dig_p[18];
    err = i2c_master_transmit_receive(bmp_handle, &reg_dig_p, 1, buf_dig_p, sizeof(buf_dig_p), -1);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) reading pressure calib", esp_err_to_name(err));
        return err;
    }

    cal_dig_p.dig_P1 =          buf_dig_p[0]  | ((uint16_t)buf_dig_p[1]  << 8);
    cal_dig_p.dig_P2 = (int16_t)(buf_dig_p[2]  | ((uint16_t)buf_dig_p[3]  << 8));
    cal_dig_p.dig_P3 = (int16_t)(buf_dig_p[4]  | ((uint16_t)buf_dig_p[5]  << 8));
    cal_dig_p.dig_P4 = (int16_t)(buf_dig_p[6]  | ((uint16_t)buf_dig_p[7]  << 8));
    cal_dig_p.dig_P5 = (int16_t)(buf_dig_p[8]  | ((uint16_t)buf_dig_p[9]  << 8));
    cal_dig_p.dig_P6 = (int16_t)(buf_dig_p[10] | ((uint16_t)buf_dig_p[11] << 8));
    cal_dig_p.dig_P7 = (int16_t)(buf_dig_p[12] | ((uint16_t)buf_dig_p[13] << 8));
    cal_dig_p.dig_P8 = (int16_t)(buf_dig_p[14] | ((uint16_t)buf_dig_p[15] << 8));
    cal_dig_p.dig_P9 = (int16_t)(buf_dig_p[16] | ((uint16_t)buf_dig_p[17] << 8));

    log_msg(TAG, "MPU9250 initialized");

    return ESP_OK;
}

static int16_t accel_offset_x = 0, accel_offset_y = 0, accel_offset_z = 0;

static esp_err_t calibrate_accel_offset(void) {
    const int N = 200;
    int32_t sum_x = 0, sum_y = 0, sum_z = 0;
    mpu_info_t sample;

    log_msg(TAG, "Calibrating accel offset, keep car still...");
    for (int i = 0; i < N; i++) {
        esp_err_t err = get_mpu_info(&sample);
        if (err != ESP_OK) return err;
        sum_x += (int16_t)sample.accel_x;
        sum_y += (int16_t)sample.accel_y;
        sum_z += (int16_t)sample.accel_z;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    accel_offset_x = sum_x / N;
    accel_offset_y = sum_y / N;
    accel_offset_z = sum_z / N;  // attention: sur Z, soustrais 1g (16384 LSB @ ±2g) avant de stocker si tu veux Z=0 au repos, pas Z=g
    log_msg(TAG, "Offsets: x=%d y=%d z=%d", accel_offset_x, accel_offset_y, accel_offset_z);
    return ESP_OK;
}

//convert function from bosch
static esp_err_t convert_temperature(int32_t *raw_temp) {

    int32_t v1, v2, T;
    v1 = (((*raw_temp >> 3) - ((int32_t)cal_dig_t.dig_T1 << 1)) * (int32_t)cal_dig_t.dig_T2) >> 11;
    v2 = (((((*raw_temp >> 4) - (int32_t)cal_dig_t.dig_T1) *
        ((*raw_temp >> 4) - (int32_t)cal_dig_t.dig_T1)) >> 12) * (int32_t)cal_dig_t.dig_T3) >> 14;
    t_fine = v1 + v2;
    T = (t_fine * 5 + 128) >> 8;

    *raw_temp = T;

    return ESP_OK;
}

/**
 * Convert raw BMP280 pressure ADC to Pa (int32 version, datasheet BST-BMP280-DS001)
 * MUST be called AFTER convert_temperature() — using t_fine
 * @param raw_pressure in/out 32 bit pressure
 */
static esp_err_t convert_pressure(int32_t *raw_pressure) {
    if (raw_pressure == NULL) return ESP_ERR_INVALID_ARG;

    int32_t  var1, var2;
    uint32_t p;
    
    var1 = ((int32_t)t_fine >> 1) - (int32_t)64000;
    var2 = (((var1 >> 2) * (var1 >> 2)) >> 11) * (int32_t)cal_dig_p.dig_P6;
    var2 = var2 + ((var1 * (int32_t)cal_dig_p.dig_P5) << 1);
    var2 = (var2 >> 2) + ((int32_t)cal_dig_p.dig_P4 << 16);
    var1 = (((int32_t)cal_dig_p.dig_P3 * (((var1 >> 2) * (var1 >> 2)) >> 13) >> 3)
            + (((int32_t)cal_dig_p.dig_P2 * var1) >> 1)) >> 18;
    var1 = ((32768 + var1) * (int32_t)cal_dig_p.dig_P1) >> 15;

    if (var1 == 0) {
        log_msg(TAG, "Pressure: division by zero, check P1 calibration");
        return ESP_ERR_INVALID_STATE;
    }

    p = (uint32_t)((int32_t)1048576 - *raw_pressure);
    p = (p - (uint32_t)(var2 >> 12)) * 3125;

    if (p < 0x80000000U) {
        p = (p << 1) / (uint32_t)var1;
    } else {
        p = (p / (uint32_t)var1) * 2;
    }

    var1 = ((int32_t)cal_dig_p.dig_P9 * (int32_t)(((p >> 3) * (p >> 3)) >> 13)) >> 12;
    var2 = ((int32_t)(p >> 2) * (int32_t)cal_dig_p.dig_P8) >> 13;
    p = (uint32_t)((int32_t)p + ((var1 + var2 + cal_dig_p.dig_P7) >> 4));

    *raw_pressure = (int32_t)p; 
    return ESP_OK;
}

/**
 * Getting MPU info
 * @returns ESP_OK success, others error 
 */
esp_err_t get_mpu_info(mpu_info_t *mpu_info) {
    if (mpu_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err;

    uint8_t reg = 0x3B;
    uint8_t buf[14];  // 14 bytes : accel(6) + temp(2) + gyro(6)
    err = i2c_master_transmit_receive(mpu_handle, &reg, 1, buf, sizeof(buf), -1);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) receiving mpu info", esp_err_to_name(err));
        return err;
    }

    uint8_t reg_bmp = 0xF7;
    uint8_t buf_bmp[6];  // 6 bytes : temp(3) + pressure(3)
    err = i2c_master_transmit_receive(bmp_handle, &reg_bmp, 1, buf_bmp, sizeof(buf_bmp), -1);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) receiving mpu info", esp_err_to_name(err));
        return err;
    }

    mpu_info->accel_x = (int16_t)(((uint16_t)buf[0] << 8) | buf[1]) - accel_offset_x;
    mpu_info->accel_y = (int16_t)(((uint16_t)buf[2] << 8) | buf[3]) - accel_offset_y;
    mpu_info->accel_z = (int16_t)(((uint16_t)buf[4] << 8) | buf[5]) - accel_offset_z;
    mpu_info->temp_mpu    = ((uint16_t)buf[6]  << 8) | buf[7];
    mpu_info->gyro_x  = ((uint16_t)buf[8]  << 8) | buf[9];
    mpu_info->gyro_y  = ((uint16_t)buf[10] << 8) | buf[11];
    mpu_info->gyro_z  = ((uint16_t)buf[12] << 8) | buf[13];

    //xlsb : first four bytes [7:4]
    mpu_info->pressure = (buf_bmp[0] << 12) | (buf_bmp[1] << 4) | (buf_bmp[2] >> 4); //20 bits word
    mpu_info->temp_bmp = (buf_bmp[3] << 12) | (buf_bmp[4] << 4) | (buf_bmp[5] >> 4);

    err = convert_temperature(&mpu_info->temp_bmp);
    if (err != ESP_OK) return err;

    err = convert_pressure(&mpu_info->pressure);
    if (err != ESP_OK) return err;

    //other formulas on datasheet

    return ESP_OK;
}

#define MPU_PAYLOAD_SIZE (7 * sizeof(int16_t) + 2 * sizeof(int32_t))

static void serialize_mpu(mpu_info_t *mpu, uint8_t *buf) {
    uint16_t len = HEADER_SENSOR_SIZE;
    memcpy(&buf[len] ,&mpu->accel_x, sizeof(int16_t));
    len += sizeof(int16_t);
    memcpy(&buf[len] ,&mpu->accel_y, sizeof(int16_t));
    len += sizeof(int16_t);
    memcpy(&buf[len] ,&mpu->accel_z, sizeof(int16_t));
    len += sizeof(int16_t);
    memcpy(&buf[len] ,&mpu->gyro_x, sizeof(int16_t));
    len += sizeof(int16_t);
    memcpy(&buf[len] ,&mpu->gyro_y, sizeof(int16_t));
    len += sizeof(int16_t);
    memcpy(&buf[len] ,&mpu->gyro_z, sizeof(int16_t));
    len += sizeof(int16_t);
    memcpy(&buf[len] ,&mpu->temp_mpu, sizeof(int16_t));
    len += sizeof(int16_t);
    memcpy(&buf[len] ,&mpu->pressure, sizeof(int32_t));
    len += sizeof(int32_t);
    memcpy(&buf[len] ,&mpu->temp_bmp, sizeof(int32_t));
}

/**
 * 50 ms monitoring MPU task
 */
static void mpu_task(void * params) {
    esp_err_t err;
    mpu_info_t mpu_info;

    err = init_mpu();
    if (err != ESP_OK){
        vTaskDelete(NULL);
        return;
    }

    err = calibrate_accel_offset();
    if (err != ESP_OK){
        vTaskDelete(NULL);
        return;
    }

    while (monitoring)
    {
        err = get_mpu_info(&mpu_info);

        if (err == ESP_OK) {
            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_MPU9250;
            uint8_t buf[HEADER_SENSOR_SIZE + MPU_PAYLOAD_SIZE];
            serialize_header(&header, buf);
            serialize_mpu(&mpu_info, buf);
            //print_mpu(&mpu_info);
            
        #if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
        #endif
        #if CONFIG_USE_ESPNOW
        #endif
        }

        vTaskDelay(pdMS_TO_TICKS(MPU_PERIOD));
    }
    vTaskDelete(NULL);
}
#endif

#if CONFIG_USE_RFID_RC522

#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   21
#define PIN_NUM_RST  22

#define RC522_REG_VERSION  0x37
#define RC522_REG_TX_CONTROL 0x14

// Registres du RC522
#define RC522_REG_COMMAND      0x01
#define RC522_REG_COMM_EN      0x02
#define RC522_REG_COMM_IRQ     0x04
#define RC522_REG_ERROR        0x06
#define RC522_REG_FIFO_DATA    0x09
#define RC522_REG_FIFO_LEVEL   0x0A
#define RC522_REG_CONTROL      0x0C
#define RC522_REG_BIT_FRAMING  0x0D

// RC522 commands [18.3]
#define PCD_IDLE               0x00
#define PCD_TRANSCEIVE         0b00001100 // Transmetting then receiving

// Commandes des Cartes/Badges (PICC)
#define PICC_REQA              0x26 // Request Type A (cherche une carte)
#define PICC_ANTICOLL          0x93 // Anti-collision (demande l'UID)
#define PICC_HALT              0x50 // Met la carte en sommeil

#define RC522_REG_MODE          0x11  // Registre de définition du mode de transmission
#define RC522_REG_TX_ASK        0x15  // Registre de configuration de la modulation TX
#define RC522_REG_RF_CFG        0x26  // Registre du Gain de l'antenne

static void rc522_write_reg(spi_device_handle_t spi, uint8_t reg, uint8_t val) {
    uint8_t tx_data[2];
    
    // SPI Address [10.2.4] (MSB = 0 = write) (LSB = 0)
    // reading bit 1-6
    tx_data[0] = (reg << 1) & 0b01111110; 

    // Sending value after [10.2.3]
    tx_data[1] = val;

    spi_transaction_t t = {
        .length = 16,           // in bits
        .tx_buffer = tx_data,
        .rx_buffer = NULL       //no return value expected
    };

    // Sends to bus, blocking function
    spi_device_polling_transmit(spi, &t);

    // if more than 16 bits, it may be better to go with ISR (aka without polling)
}

static uint8_t rc522_read_reg(spi_device_handle_t spi, uint8_t reg) {
    uint8_t tx_data[2];
    uint8_t rx_data[2];
    
    // SPI Address (MSB = 1 = read) (LSB = 0)
    tx_data[0] = ((reg << 1) & 0x7E) | 0x80; 
    tx_data[1] = 0x00; // dummy byte because the slave sends its response at the next 8 cycles

    spi_transaction_t t = {
        .length = 16,           // in bits
        .tx_buffer = tx_data,
        .rx_buffer = rx_data
    };

    // Send (MOSI) and receive (MISO)
    spi_device_polling_transmit(spi, &t);

    // it is possible to burst (= send 1 address, receive several data bytes)
    
    // first byte is garbage (when address is sent)
    return rx_data[1]; 
}

static esp_err_t rc522_to_card(spi_device_handle_t spi, uint8_t command, 
                               uint8_t *send_data, uint8_t send_len, 
                               uint8_t *back_data, uint32_t *back_len) {

    //Command to IDLE [9.2.1.2]
    rc522_write_reg(spi, RC522_REG_COMMAND, PCD_IDLE);

    //[9.2.1.5] : puts to 0 (bit7) others bits 6-0 (Interruption flags)
    rc522_write_reg(spi, RC522_REG_COMM_IRQ, 0b01111111);
    
    //[9.2.1.11] flush buffer (bit 7 = 1)
    rc522_write_reg(spi, RC522_REG_FIFO_LEVEL, 0x80); 

    // [9.2.1.10] I/O 64 bytes fifo buffer writing
    for (int i = 0; i < send_len; i++) {
        rc522_write_reg(spi, RC522_REG_FIFO_DATA, send_data[i]);
    }

    // executing command 
    rc522_write_reg(spi, RC522_REG_COMMAND, command);
    if (command == PCD_TRANSCEIVE) {
        // [9.2.1.14] bit7 = 1 = startsend, after reading bit 6-0 already configured
        rc522_write_reg(spi, RC522_REG_BIT_FRAMING, rc522_read_reg(spi, RC522_REG_BIT_FRAMING) | 0x80);
    }

    // Waiting for flags to be set
    int64_t timeout_time = esp_timer_get_time() + (25 * 1000); // 25ms timeout
    uint8_t n;
    bool response = false;
    do {
        n = rc522_read_reg(spi, RC522_REG_COMM_IRQ);

        if ((n & 0x01) || (n & 0b00110000)) {
            response = true;
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
        // when error (0x01) or transmission finished (48) [Rx & Idle]
    } while (esp_timer_get_time() < timeout_time); 

    // stop startsend (actually the module sets it to 0 when FIFO is empty, but just in case..)
    rc522_write_reg(spi, RC522_REG_BIT_FRAMING, rc522_read_reg(spi, RC522_REG_BIT_FRAMING) & 0x7F);

    if (!response) return ESP_ERR_TIMEOUT;

    // verify errors [9.2.1.7]
    uint8_t error_reg = rc522_read_reg(spi, RC522_REG_ERROR);
    // BufOverflow, CollErr, CRCErr, ParityErr, ProtocolErr
    if (error_reg & 0b00011111) return ESP_FAIL; 

    // getting data
    if (back_data != NULL && back_len != NULL) {
        //read number of bytes in fifo
        uint8_t fifo_len = rc522_read_reg(spi, RC522_REG_FIFO_LEVEL);
        if (fifo_len > *back_len) fifo_len = *back_len;
        
        *back_len = fifo_len;
        for (int i = 0; i < fifo_len; i++) {
            //read data from fifo
            back_data[i] = rc522_read_reg(spi, RC522_REG_FIFO_DATA);
        }
    }

    return ESP_OK;
}

static esp_err_t rc522_is_card_present(spi_device_handle_t spi) {
    uint8_t req_cmd = PICC_REQA;
    uint32_t back_bits = 0;
    
    // TxLastBits to 7: transmitting bytes to tag
    rc522_write_reg(spi, RC522_REG_BIT_FRAMING, 0b00000111);
    
    //send command to card: wake it up
    return rc522_to_card(spi, PCD_TRANSCEIVE, &req_cmd, 1, NULL, &back_bits);
}

static esp_err_t rc522_get_card_uid(spi_device_handle_t spi, uint8_t *uid, uint8_t *uid_len) {
    uint8_t valid_bits = 0;
    uint32_t back_len = 10; // Taille max de notre buffer de réception
    
    // Anticollision (for multiple cards) then reset
    uint8_t send_data[2] = { PICC_ANTICOLL, 0x20 };
    
    //TxLastBits, RxLastBits to 0 : comms with tag bit by bit
    rc522_write_reg(spi, RC522_REG_BIT_FRAMING, 0x00);
    
    //send data to card
    esp_err_t err = rc522_to_card(spi, PCD_TRANSCEIVE, send_data, 2, uid, &back_len);
    if (err == ESP_OK) {
        *uid_len = back_len - 1; // last byte = checksum
    }
    return err;
}

static void rc522_card_halt(spi_device_handle_t spi) {
    uint32_t back_len = 0;
    //ISO 14443A : 4 bytes for HALT : 2 commands + 2 crc on those 2 previous ones
    uint8_t send_data[4] = { PICC_HALT, 0x00, 0x00, 0x00 };
    
    // Hardcoded CRC for 0x50 then 0x00
    // 0x6363 and x^16 + x^12 + x^5 + 1
    send_data[2] = 0x57; 
    send_data[3] = 0xCD;

    rc522_to_card(spi, PCD_TRANSCEIVE, send_data, 4, NULL, &back_len);
}

static spi_device_handle_t rc522_spi_handle;

static esp_err_t init_rfid_rc522() {
    esp_err_t err;

    // master bus cfg
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096
    };

    err = spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) init SPI bus", esp_err_to_name(err));
        return err;
    }

    //cf datasheet
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 4 * 1000 * 1000,  
        .mode = 0,                              
        .spics_io_num = PIN_NUM_CS,             
        .queue_size = 7,                   
    };

    err = spi_bus_add_device(SPI3_HOST, &devcfg, &rc522_spi_handle);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) init SPI bus", esp_err_to_name(err));
        return err;
    }

    // hard reset module
    gpio_reset_pin(PIN_NUM_RST);
    gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    // rc522 version
    uint8_t version = rc522_read_reg(rc522_spi_handle, RC522_REG_VERSION);
    
    if (version == 0x00 || version == 0xFF) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "RC522 not responding");
        return ESP_FAIL;
    } else {
        log_msg(TAG, "RC522 detected, version: 0x%02X", version);
    }

    //activate antenna
    uint8_t tx_control = rc522_read_reg(rc522_spi_handle, RC522_REG_TX_CONTROL);
    if ((tx_control & 0b00000011) != 0b00000011) { //if tx1 & tx2 not set
        //then activate
        rc522_write_reg(rc522_spi_handle, RC522_REG_TX_CONTROL, tx_control | 0b00000011);
        log_msg(TAG, "Antenna 13.56Mhz activated");
    }

    // 100% ASK : cut emission at 100% then enables it to send bits
    rc522_write_reg(rc522_spi_handle, RC522_REG_TX_ASK, 0b01000000); 

    // RxGain to 48dB and reserved to 1000 (default value)
    rc522_write_reg(rc522_spi_handle, RC522_REG_RF_CFG, 0b01111000);

    return err;
}

typedef struct rfid_rc522_info_st {
    uint8_t uid[16];
} rfid_rc522_info_t;

static void rfid_rc522_task(void * params) {
    esp_err_t err;

    err = init_rfid_rc522();
    if (err != ESP_OK){
        vTaskDelete(NULL);
        return;
    }

    rfid_rc522_info_t rfid_info = {0};
    uint8_t uid_len = 0;

    while (monitoring) {

        log_msg_lvl(ESP_LOG_DEBUG, TAG, "Antenna listening for 13.56MHz tag...");

        //ping to see if a card is here
        esp_err_t present_err = rc522_is_card_present(rc522_spi_handle);
        
        if (present_err == ESP_OK) {
            log_msg_lvl(ESP_LOG_DEBUG, TAG, "RF signal received, Reading UID...");
            
            esp_err_t uid_err = rc522_get_card_uid(rc522_spi_handle, rfid_info.uid, &uid_len);
            if (uid_err == ESP_OK) {

                if(uid_len > sizeof(rfid_info.uid)) {
                    uid_len = sizeof(rfid_info.uid);
                }
                
                log_msg_lvl(ESP_LOG_DEBUG, TAG, "Tag decoded");
                ESP_LOG_BUFFER_HEX(TAG, rfid_info.uid, uid_len);

                header_sensor_t header = {0};
                header.esp_id = (uint8_t)CONFIG_ESP_ID;
                header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
                header.type = SENSOR_TYPE_RFID_RC522;
                uint8_t buf[HEADER_SENSOR_SIZE + uid_len];
                serialize_header(&header, buf);
                memcpy(&buf[HEADER_SENSOR_SIZE], rfid_info.uid, uid_len);
                
            #if CONFIG_USE_UDPLIB
                send_udp_sensor(buf, sizeof(buf));
            #endif

                rc522_card_halt(rc522_spi_handle);
            } else {
                log_msg_lvl(ESP_LOG_WARN, TAG, "Error reading UID (%s)", esp_err_to_name(uid_err));
            }
        } else {
            log_msg_lvl(ESP_LOG_DEBUG, TAG, "No card detected (%s)", esp_err_to_name(present_err));
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }

    vTaskDelete(NULL);
}

#endif

#if CONFIG_USE_RCWL_0515

#define OUT_PIN 35

static SemaphoreHandle_t sem_rcwl = NULL;

static void IRAM_ATTR rcwl_out_isr_handler(void* arg)
{
    BaseType_t taskAwoken = pdFALSE;
    
    xSemaphoreGiveFromISR(sem_rcwl, &taskAwoken);
    portYIELD_FROM_ISR(taskAwoken); //wake up the task waiting for semaphore
}

static esp_err_t init_rcwl_0515() {

    esp_err_t err;

    if (sem_rcwl != NULL) {
        log_msg(TAG, "RCWL-0515 already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    sem_rcwl = xSemaphoreCreateBinary();
    if (sem_rcwl == NULL) {
        log_msg(TAG, "Error creating semaphore");
        return ESP_ERR_NOT_ALLOWED;
    }

    err = gpio_reset_pin(OUT_PIN);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), OUT_PIN);
        return err;
    }

    err = gpio_set_direction(OUT_PIN, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction pin %d", esp_err_to_name(err), OUT_PIN);
        return err;
    }

    err = gpio_set_intr_type(OUT_PIN, GPIO_INTR_ANYEDGE);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting interrupt type pin %d", esp_err_to_name(err), OUT_PIN);
        return err;
    }
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        log_msg(TAG, "Error (%s) installing isr pin %d", esp_err_to_name(err), OUT_PIN);
        return err;
    }
    err = gpio_isr_handler_add(OUT_PIN, rcwl_out_isr_handler, NULL);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding isr handler pin %d", esp_err_to_name(err), OUT_PIN);
        return err;
    }

    log_msg(TAG, "RCWL-0515 initialized");
    return ESP_OK;
}

static void rcwl_task(void * params) {
    esp_err_t err;
    
    err = init_rcwl_0515();
    if (err != ESP_OK){
        vTaskDelete(NULL);
        return;
    }

    bool detected_motion = false;

    while (monitoring)
    {
        if (xSemaphoreTake(sem_rcwl, portMAX_DELAY) == pdTRUE) {
            detected_motion = gpio_get_level(OUT_PIN);

            log_msg(TAG, "edge value : %d", detected_motion);
    
            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_RCWL_0515;
            uint8_t buf[HEADER_SENSOR_SIZE + 1];
            serialize_header(&header, buf);
            buf[HEADER_SENSOR_SIZE] = detected_motion;
            
        #if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
        #endif
        #if CONFIG_USE_ESPNOW
        #endif
        }
    }
    vTaskDelete(NULL);
}

#endif

#if CONFIG_USE_VL53L1X
#define VL_ADDR 0x29
#define VL_PERIOD 50 

#define XSHUT_GPIO 23

#define VL_REG_IDENTIFICATION 0x010F
#define VL_REG_FIRMWARE_BOOT  0x00E5
#define VL_REG_GPIO_STATUS   0x0031
#define VL_REG_INT_CLEAR      0x0086
#define VL_REG_START_RANGING  0x0087
#define VL_REG_RESULT_DIST    0x0096
#define VL_REG_RANGE_STATUS   0x0089

typedef struct {
    uint16_t distance; 
    uint8_t range_status;
} vl_info_t;
i2c_master_dev_handle_t vl_handle;

i2c_device_config_t vl_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7, 
    .device_address = VL_ADDR,             
    .scl_speed_hz = 400000,                
};

static const uint8_t VL53L1X_DEFAULT_CONFIGURATION[] = {
    0x12, 0x00, 0x00, 0x11, 0x02, 0x00, 0x02, 0x08, 0x00, 0x08, 0x10, 0x01, 0x01, 0x00, 0x00, 0x00,
    0x00, 0xff, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x0b, 0x00, 0x00, 0x02, 0x0a, 0x21,
    0x00, 0x00, 0x05, 0x05, 0x15, 0x05, 0x03, 0x08, 0x03, 0x08, 0x35, 0x00, 0x03, 0x04, 0x03, 0x08,
    0x01, 0x01, 0x01, 0x00, 0x01, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x20, 0x0b, 0x00, 0x00, 0x02, 0x0a,
    0x00, 0x00, 0x03, 0x01, 0x00, 0x02, 0x01, 0x01, 0x00, 0x01, 0x0c, 0x08, 0x01, 0x00, 0x00, 0x08,
    0x01, 0x14, 0x00, 0x00, 0x00, 0x07, 0x03, 0x05, 0x05, 0x15, 0x03
};

esp_err_t init_vl() {
    esp_err_t err;

    if (master_handle == NULL) {
        err = i2c_new_master_bus(&master_cfg, &master_handle);
        if (err != ESP_OK) {
            log_msg(TAG, "Error (%s) allocating I²C master bus", esp_err_to_name(err));
            return err;
        }
    }

    err = i2c_master_bus_add_device(master_handle, &vl_cfg, &vl_handle);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding device to master bus", esp_err_to_name(err));
        return err;
    }

// 1. Vérification ID
    uint8_t reg_id[2] = { (VL_REG_IDENTIFICATION >> 8) & 0xFF, VL_REG_IDENTIFICATION & 0xFF };
    uint8_t buf_rec[2] = {0};
    err = i2c_master_transmit_receive(vl_handle, reg_id, 2, buf_rec, 2, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) reading sensor identification. Vérifie le câblage et XSHUT !", esp_err_to_name(err));
        return err;
    }

    uint16_t sensor_id = ((uint16_t)buf_rec[0] << 8) | buf_rec[1];
    if (sensor_id != 0xEACC) {
        log_msg(TAG, "Error: Incorrect sensor ID 0x%04X (Attendu 0xEACC)", sensor_id);
        return ESP_ERR_INVALID_VERSION;
    }

    // 2. Attente du boot du firmware interne
    uint8_t boot_state = 0;
    uint8_t reg_boot[2] = { (VL_REG_FIRMWARE_BOOT >> 8) & 0xFF, VL_REG_FIRMWARE_BOOT & 0xFF };
    int retry = 50;
    
    while (retry > 0) {
        err = i2c_master_transmit_receive(vl_handle, reg_boot, 2, &boot_state, 1, pdMS_TO_TICKS(50));
        if (err == ESP_OK && (boot_state & 0x01) == 1) {
            break; 
        }
        vTaskDelay(pdMS_TO_TICKS(2));
        retry--;
    }

    if (retry == 0) {
        log_msg(TAG, "Error: Sensor firmware boot timeout (0x%02X)", boot_state);
        return ESP_ERR_TIMEOUT;
    }

    // 3. Écriture de la table de configuration d'usine globale
    uint8_t init_buf[2 + sizeof(VL53L1X_DEFAULT_CONFIGURATION)];
    init_buf[0] = 0x00;
    init_buf[1] = 0x2D;
    memcpy(&init_buf[2], VL53L1X_DEFAULT_CONFIGURATION, sizeof(VL53L1X_DEFAULT_CONFIGURATION));
    err = i2c_master_transmit(vl_handle, init_buf, sizeof(init_buf), pdMS_TO_TICKS(200));
    if (err != ESP_OK) {
        log_msg(TAG, "Error loading ULD init table");
        return err;
    }

    // Configuration Timing budget (50ms)
    uint8_t buf_tb1[3] = { 0x00, 0x5E, 0x00 };
    i2c_master_transmit(vl_handle, buf_tb1, 3, pdMS_TO_TICKS(50));
    uint8_t buf_tb2[3] = { 0x00, 0x5F, 0x1E };
    i2c_master_transmit(vl_handle, buf_tb2, 3, pdMS_TO_TICKS(50));

    // Démarrage de la télémétrie
    uint8_t buf_start[3] = { 0x00, 0x87, 0x40 };
    err = i2c_master_transmit(vl_handle, buf_start, 3, pdMS_TO_TICKS(50));
    if (err != ESP_OK) return err;

    // Clear initial de l'interruption pour lancer la première capture physique
    uint8_t buf_clear[3] = { 0x00, 0x86, 0x01 };
    err = i2c_master_transmit(vl_handle, buf_clear, 3, pdMS_TO_TICKS(50));
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) clearing initial interrupt", esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(100)); // Laisser le temps au système de se stabiliser

    // Après le buf_start et buf_clear, avant le log "fully booted"
    uint8_t reg_check[2];
    uint8_t val = 0;

    reg_check[0] = 0x00; reg_check[1] = 0x87;
    i2c_master_transmit_receive(vl_handle, reg_check, 2, &val, 1, -1);
    log_msg(TAG, "0x0087 (MODE_START) = 0x%02X (attendu 0x40)", val);

    reg_check[0] = 0x00; reg_check[1] = 0x86;
    i2c_master_transmit_receive(vl_handle, reg_check, 2, &val, 1, -1);
    log_msg(TAG, "0x0086 (INT_CLEAR) = 0x%02X", val);

    reg_check[0] = 0x00; reg_check[1] = 0x31;
    i2c_master_transmit_receive(vl_handle, reg_check, 2, &val, 1, -1);
    log_msg(TAG, "0x0031 (GPIO_STATUS) = 0x%02X (attendu 0x03 avant mesure)", val);

    // Attente active 200ms pour laisser le temps au capteur de faire une mesure
    vTaskDelay(pdMS_TO_TICKS(200));

    reg_check[0] = 0x00; reg_check[1] = 0x31;
    i2c_master_transmit_receive(vl_handle, reg_check, 2, &val, 1, -1);
    log_msg(TAG, "0x0031 après 200ms = 0x%02X (attendu 0x01 si mesure prête)", val);

    reg_check[0] = 0x00; reg_check[1] = 0x89;
    i2c_master_transmit_receive(vl_handle, reg_check, 2, &val, 1, -1);
    log_msg(TAG, "0x0089 (RANGE_STATUS) = 0x%02X", val);

    

    log_msg(TAG, "VL53L1X fully booted and operational");
    return ESP_OK;
}
esp_err_t get_vl_info(vl_info_t *vl_info) {
    if (vl_info == NULL) return ESP_ERR_INVALID_ARG;

    esp_err_t err;
    uint8_t reg_ptr[2];
    uint8_t buf_rec[2];
    uint8_t status = 0;

    // 1. Lecture de l'état de la mesure via le registre 0x0031
    reg_ptr[0] = (VL_REG_GPIO_STATUS >> 8) & 0xFF;
    reg_ptr[1] = VL_REG_GPIO_STATUS & 0xFF;

    err = i2c_master_transmit_receive(vl_handle, reg_ptr, 2, &status, 1, pdMS_TO_TICKS(50));
    if (err != ESP_OK) return err;

    // 💡 Avec 0x0031, la valeur 0x03 signifie "pas encore prêt"
    if (status == 0x03) {
        return ESP_ERR_NOT_FINISHED;
    }

    // 2. Lecture du Diagnostic Range Status (Registre 0x0089)
    uint8_t reg_status[2] = { (VL_REG_RANGE_STATUS >> 8) & 0xFF, VL_REG_RANGE_STATUS & 0xFF };
    err = i2c_master_transmit_receive(vl_handle, reg_status, 2, &vl_info->range_status, 1, pdMS_TO_TICKS(50));
    if (err != ESP_OK) return err;

    // 3. Lecture de la distance calculée (2 octets Big Endian depuis 0x0096)
    reg_ptr[0] = (VL_REG_RESULT_DIST >> 8) & 0xFF;
    reg_ptr[1] = VL_REG_RESULT_DIST & 0xFF;

    err = i2c_master_transmit_receive(vl_handle, reg_ptr, 2, buf_rec, 2, pdMS_TO_TICKS(50));
    if (err != ESP_OK) return err;
    
    vl_info->distance = ((uint16_t)buf_rec[0] << 8) | buf_rec[1];

    // 4. Libération de l'interruption (Obligatoire pour que le capteur fasse la mesure suivante)
    uint8_t buf_clear[3] = { (VL_REG_INT_CLEAR >> 8) & 0xFF, VL_REG_INT_CLEAR & 0xFF, 0x01 };
    err = i2c_master_transmit(vl_handle, buf_clear, 3, pdMS_TO_TICKS(50));
    if (err != ESP_OK) return err;

    return ESP_OK;
}
#define VL_PAYLOAD_SIZE (sizeof(uint16_t))

static void serialize_vl(vl_info_t *vl, uint8_t *buf) {
    uint16_t len = HEADER_SENSOR_SIZE;
    memcpy(&buf[len], &vl->distance, sizeof(uint16_t));
}

static void vl_task(void * params) {
    esp_err_t err;
    vl_info_t vl_info;

    err = init_vl();
    if (err != ESP_OK){
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) init vl", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    while (monitoring)
    {
         uint8_t reg_ptr[2] = { 0x00, 0x31 };
        uint8_t status = 0;
        i2c_master_transmit_receive(vl_handle, reg_ptr, 2, &status, 1, -1);
        log_msg(TAG, "GPIO status 0x0031 = 0x%02X", status);  // <-- debug

        err = get_vl_info(&vl_info);

        if (err == ESP_OK) {
            // 💡 Analyse du Range Status selon la Datasheet ST :
            // 0 = Mesure Parfaite
            // 1 = Sigma Failure (Trop de pollution lumineuse ambiante)
            // 2 = Signal Failure (Cible trop sombre ou trop loin, pas assez de retour laser)
            // 4 = Out of Bounds (Cible hors de portée)
            if (vl_info.range_status == 0) {
                log_msg(TAG, "dist : %u mm", vl_info.distance);
            } else {
                log_msg(TAG, "dist brute : %u mm (Statut d'erreur laser: %d)", vl_info.distance, vl_info.range_status);
            }
            
            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_VL53L1X; 
            uint8_t buf[HEADER_SENSOR_SIZE + VL_PAYLOAD_SIZE];
            
            serialize_header(&header, buf);
            serialize_vl(&vl_info, buf);
            
        #if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
        #endif
        } 
        else if (err == ESP_ERR_NOT_FINISHED) {
            // Le capteur est en cours d'acquisition active. On passe calmement au cycle suivant.
        } 
        else {
            log_msg_lvl(ESP_LOG_ERROR, TAG, "Erreur critique de communication I2C (%s)", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(VL_PERIOD));
    }
    vTaskDelete(NULL);
}

#endif

#if CONFIG_USE_AS5600

#define AS5600_SENS_ADDR            0x36   
#define AS5600_REG_ANGLE_MSB        0x0E   

i2c_device_config_t as_config = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = AS5600_SENS_ADDR,
    .scl_speed_hz = 400000,
};
i2c_master_dev_handle_t as_handle;

esp_err_t init_as() {
    esp_err_t err;

    if (master_handle == NULL) {
        err = i2c_new_master_bus(&master_cfg, &master_handle);
        if (err != ESP_OK) {
            log_msg(TAG, "Error (%s) allocating I²C master bus", esp_err_to_name(err));
            return err;
        }
    }

    err = i2c_master_bus_add_device(master_handle, &as_config, &as_handle);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding device to master bus", esp_err_to_name(err));
        return err;
    }

    log_msg(TAG, "AS5600 initialized");
    return ESP_OK;
}

    
static void as_task(void * params) {
    esp_err_t err;

    err = init_as();
    if (err != ESP_OK){
        vTaskDelete(NULL);
        return;
    }

    uint8_t reg_addr = AS5600_REG_ANGLE_MSB;
    uint8_t data_rd[2] = {0};

    monitoring = true;
    while (monitoring)
    {
        err = i2c_master_transmit_receive(as_handle, &reg_addr, 1, data_rd, 2, -1);

        if (err == ESP_OK) {

            uint16_t raw_angle = ((data_rd[0] & 0x0F) << 8) | data_rd[1];
            float degrees = (raw_angle * 360.0f) / 4096.0f;

            log_msg(TAG, "Raw angle : %d | Deg: %.2f°", raw_angle, degrees);
            
            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_AS5600; 
            uint8_t buf[HEADER_SENSOR_SIZE + 2];
            
            serialize_header(&header, buf);
            memcpy(&buf[HEADER_SENSOR_SIZE], data_rd, 2);
            
        #if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
        #endif
        } else {
            log_msg_lvl(ESP_LOG_ERROR, TAG, "I2C comm error (%s)", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}
#endif

#if CONFIG_USE_KY003

#define OUT_PIN_KY_003 23

static volatile uint8_t pulse_count = 0;
static portMUX_TYPE pulse_spinlock = portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR ky_003_out_isr_handler(void* arg)
{
    taskENTER_CRITICAL_ISR(&pulse_spinlock);
    pulse_count++;
    taskEXIT_CRITICAL_ISR(&pulse_spinlock);
}

static esp_err_t init_ky_003() {

    esp_err_t err;

    err = gpio_reset_pin(OUT_PIN_KY_003);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), OUT_PIN_KY_003);
        return err;
    }

    err = gpio_set_direction(OUT_PIN_KY_003, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction pin %d", esp_err_to_name(err), OUT_PIN_KY_003);
        return err;
    }

    err = gpio_set_intr_type(OUT_PIN_KY_003, GPIO_INTR_POSEDGE);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting interrupt type pin %d", esp_err_to_name(err), OUT_PIN_KY_003);
        return err;
    }
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        log_msg(TAG, "Error (%s) installing isr pin %d", esp_err_to_name(err), OUT_PIN_KY_003);
        return err;
    }
    err = gpio_isr_handler_add(OUT_PIN_KY_003, ky_003_out_isr_handler, NULL);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding isr handler pin %d", esp_err_to_name(err), OUT_PIN_KY_003);
        return err;
    }

    log_msg(TAG, "KY-003 initialized");
    return ESP_OK;
}

static void ky_003_task(void * params) {
    esp_err_t err;
    
    err = init_ky_003();
    if (err != ESP_OK){
        vTaskDelete(NULL);
        return;
    }

    uint32_t total_pulses = 0;

    while (monitoring)
    {
    
        taskENTER_CRITICAL(&pulse_spinlock);
        uint8_t temp_pulse = pulse_count;
        pulse_count = 0;
        taskEXIT_CRITICAL(&pulse_spinlock);

        total_pulses += temp_pulse;
        
        header_sensor_t header = {0};
        header.esp_id = (uint8_t)CONFIG_ESP_ID;
        header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
        header.type = SENSOR_TYPE_KY003;
        uint8_t buf[HEADER_SENSOR_SIZE + 1];
        serialize_header(&header, buf);
        buf[HEADER_SENSOR_SIZE] = temp_pulse;

        log_msg(TAG, "count: %u/%u, RPM: %.2f", temp_pulse, total_pulses, (temp_pulse / 100e-3) * 60.0);
        

    #if CONFIG_USE_UDPLIB
        send_udp_sensor(buf, sizeof(buf));
    #endif
    #if CONFIG_USE_ESPNOW
    #endif
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}
    

#endif

#if CONFIG_USE_FC33

#define OUT_PIN_FC33 23

static volatile uint8_t pulse_count_fc33 = 0;
static portMUX_TYPE pulse_spinlock_fc33 = portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR fc33_out_isr_handler(void* arg)
{
    taskENTER_CRITICAL_ISR(&pulse_spinlock_fc33);
    pulse_count_fc33++;
    taskEXIT_CRITICAL_ISR(&pulse_spinlock_fc33);
}

static esp_err_t init_fc33() {

    esp_err_t err;

    err = gpio_reset_pin(OUT_PIN_FC33);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), OUT_PIN_FC33);
        return err;
    }

    err = gpio_set_direction(OUT_PIN_FC33, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction pin %d", esp_err_to_name(err), OUT_PIN_FC33);
        return err;
    }

    err = gpio_set_intr_type(OUT_PIN_FC33, GPIO_INTR_NEGEDGE);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting interrupt type pin %d", esp_err_to_name(err), OUT_PIN_FC33);
        return err;
    }
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        log_msg(TAG, "Error (%s) installing isr pin %d", esp_err_to_name(err), OUT_PIN_FC33);
        return err;
    }
    err = gpio_isr_handler_add(OUT_PIN_FC33, fc33_out_isr_handler, NULL);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding isr handler pin %d", esp_err_to_name(err), OUT_PIN_FC33);
        return err;
    }

    log_msg(TAG, "FC-33 initialized");
    return ESP_OK;
}

static void fc_33_task(void * params) {
    esp_err_t err;
    
    err = init_fc33();
    if (err != ESP_OK){
        vTaskDelete(NULL);
        return;
    }

    uint32_t total_pulses = 0;

    while (monitoring)
    {
    
        taskENTER_CRITICAL(&pulse_spinlock_fc33);
        uint8_t temp_pulse = pulse_count_fc33;
        pulse_count_fc33 = 0;
        taskEXIT_CRITICAL(&pulse_spinlock_fc33);

        total_pulses += temp_pulse;
        
        header_sensor_t header = {0};
        header.esp_id = (uint8_t)CONFIG_ESP_ID;
        header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
        header.type = SENSOR_TYPE_FC33;
        uint8_t buf[HEADER_SENSOR_SIZE + 1];
        serialize_header(&header, buf);
        buf[HEADER_SENSOR_SIZE] = temp_pulse;

        log_msg(TAG, "count: %u/%u, RPM: %.2f", temp_pulse, total_pulses, (temp_pulse / 100e-3) * 60.0);
        

    #if CONFIG_USE_UDPLIB
        send_udp_sensor(buf, sizeof(buf));
    #endif
    #if CONFIG_USE_ESPNOW
    #endif
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}
    

#endif

#if CONFIG_USE_KY033

#define OUT_PIN_KY033 8

static pcnt_unit_handle_t pcnt_unit_ky033 = NULL;

static esp_err_t init_ky033() {

    esp_err_t err;
// 1. Configuration de l'unité de comptage matérielle
    pcnt_unit_config_t unit_config = {
        .high_limit = 20000, // Limite haute max avant overflow (sécurité)
        .low_limit = -1,     // On ne compte pas à l'envers
    };
    err = pcnt_new_unit(&unit_config, &pcnt_unit_ky033);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) creating PCNT unit", esp_err_to_name(err));
        return err;
    }

    // 2. Configuration du Glitch Filter (Anti-mitraillette matériel)
    // On ignore tout signal plus court que 50 000 ns (50 microsecondes)
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 10000,
    };
    err = pcnt_unit_set_glitch_filter(pcnt_unit_ky033, &filter_config);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting PCNT glitch filter", esp_err_to_name(err));
        return err;
    }

    // 3. Configuration du canal et assignation du GPIO 23
    pcnt_chan_config_t chan_config = {
        .edge_gpio_num = OUT_PIN_KY033,
        .level_gpio_num = -1, // Pas de pin de direction, on avance seulement
    };
    pcnt_channel_handle_t pcnt_chan = NULL;
    err = pcnt_new_channel(pcnt_unit_ky033, &chan_config, &pcnt_chan);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) creating PCNT channel", esp_err_to_name(err));
        return err;
    }

    // 4. Assignation des actions sur les fronts (Équivalent du NEGEDGE)
    // Front montant (pos) -> On maintient l'état (HOLD)
    // Front descendant (neg) -> On incrémente le compteur (INCREASE)
    err = pcnt_channel_set_edge_action(pcnt_chan, 
                                       PCNT_CHANNEL_EDGE_ACTION_HOLD, 
                                       PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting PCNT edge action", esp_err_to_name(err));
        return err;
    }

    // 5. Activation et démarrage du module autonome
    err = pcnt_unit_enable(pcnt_unit_ky033);
    if (err != ESP_OK) return err;

    err = pcnt_unit_start(pcnt_unit_ky033);
    if (err != ESP_OK) return err;

    log_msg(TAG, "KY-033 initialized");
    return ESP_OK;
}

static void ky033_task(void * params) {
    esp_err_t err;
    
    err = init_ky033();
    if (err != ESP_OK){
        vTaskDelete(NULL);
        return;
    }

    uint32_t total_pulses = 0;
    monitoring = true;

    while (monitoring)
    {
    
        int pcnt_count = 0;

        // On récupère directement la valeur lue par le silicium
        pcnt_unit_get_count(pcnt_unit_ky033, &pcnt_count);
        // On remet immédiatement à zéro pour la frame de 100ms suivante
        pcnt_unit_clear_count(pcnt_unit_ky033);

        uint8_t temp_pulse = (uint8_t)pcnt_count;

        total_pulses += temp_pulse;
        
        header_sensor_t header = {0};
        header.esp_id = (uint8_t)CONFIG_ESP_ID;
        header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
        header.type = SENSOR_TYPE_KY033;
        uint8_t buf[HEADER_SENSOR_SIZE + 2];
        serialize_header(&header, buf);
        buf[HEADER_SENSOR_SIZE] = temp_pulse;
        int16_t motor = -1001;
    #if CONFIG_USE_LEDLIB
        err = get_motor_percent(&motor);
        if (err != ESP_OK) {
            log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) reading MOTOR", esp_err_to_name(err));
        }
    #endif
        buf[HEADER_SENSOR_SIZE + 1] = motor;

        //log_msg(TAG, "count: %u/%u, RPM: %.2f", temp_pulse, total_pulses, (temp_pulse / 100e-3) * 60.0);
        
    #if CONFIG_USE_UDPLIB
        send_udp_sensor(buf, sizeof(buf));
    #endif
    #if CONFIG_USE_ESPNOW
    #endif
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}

#endif

#if CONFIG_USE_KY002

#define OUT_PIN_KY002 23

static volatile uint8_t pulse_count_ky002 = 0;
static portMUX_TYPE pulse_spinlock_ky002 = portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR ky002_out_isr_handler(void* arg)
{
    taskENTER_CRITICAL_ISR(&pulse_spinlock_ky002);
    pulse_count_ky002++;
    taskEXIT_CRITICAL_ISR(&pulse_spinlock_ky002);
}

static esp_err_t init_ky002() {

    esp_err_t err;

    err = gpio_reset_pin(OUT_PIN_KY002);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), OUT_PIN_KY002);
        return err;
    }

    err = gpio_set_direction(OUT_PIN_KY002, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction pin %d", esp_err_to_name(err), OUT_PIN_KY002);
        return err;
    }

    err = gpio_set_intr_type(OUT_PIN_KY002, GPIO_INTR_NEGEDGE);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting interrupt type pin %d", esp_err_to_name(err), OUT_PIN_KY002);
        return err;
    }
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        log_msg(TAG, "Error (%s) installing isr pin %d", esp_err_to_name(err), OUT_PIN_KY002);
        return err;
    }
    err = gpio_isr_handler_add(OUT_PIN_KY002, ky002_out_isr_handler, NULL);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding isr handler pin %d", esp_err_to_name(err), OUT_PIN_KY002);
        return err;
    }

    log_msg(TAG, "FC-33 initialized");
    return ESP_OK;
}

static void ky002_task(void * params) {
    esp_err_t err;
    
    err = init_ky002();
    if (err != ESP_OK){
        vTaskDelete(NULL);
        return;
    }

    uint32_t total_pulses = 0;

    while (monitoring)
    {
    
        taskENTER_CRITICAL(&pulse_spinlock_ky002);
        uint8_t temp_pulse = pulse_count_ky002;
        pulse_count_ky002 = 0;
        taskEXIT_CRITICAL(&pulse_spinlock_ky002);

        total_pulses += temp_pulse;
        
        header_sensor_t header = {0};
        header.esp_id = (uint8_t)CONFIG_ESP_ID;
        header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
        header.type = SENSOR_TYPE_KY002;
        uint8_t buf[HEADER_SENSOR_SIZE + 1];
        serialize_header(&header, buf);
        buf[HEADER_SENSOR_SIZE] = temp_pulse;

        log_msg(TAG, "count: %u/%u, RPM: %.2f", temp_pulse, total_pulses, (temp_pulse / 100e-3) * 60.0);
        

    #if CONFIG_USE_UDPLIB
        send_udp_sensor(buf, sizeof(buf));
    #endif
    #if CONFIG_USE_ESPNOW
    #endif
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}
    

#endif


#if CONFIG_USE_KY040

#if CONFIG_USE_LEDLIB
#include "ledLib.h"
#endif

#define SW_PIN_KY040 22
#define DT_PIN_KY040 21
#define CLK_PIN_KY040 23

static esp_err_t setup_pin(gpio_num_t gpio_num) {

    esp_err_t err = ESP_OK; 
    
    err = gpio_reset_pin(gpio_num);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), gpio_num);
        return err;
    }

    err = gpio_set_direction(gpio_num, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction pin %d", esp_err_to_name(err), gpio_num);
        return err;
    }

    err = gpio_set_intr_type(gpio_num, GPIO_INTR_ANYEDGE);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting interrupt type pin %d", esp_err_to_name(err), gpio_num);
        return err;
    }
    return err;
}

static SemaphoreHandle_t sem_ky040 = NULL;

static volatile bool clk_val = false;
static volatile bool dt_val = false;

static void IRAM_ATTR ky040_sw_isr_handler(void* arg)
{
    BaseType_t taskAwoken = pdFALSE;
    
    xSemaphoreGiveFromISR(sem_ky040, &taskAwoken);
    portYIELD_FROM_ISR(taskAwoken); //wake up the task waiting for semaphore
}

static void IRAM_ATTR ky040_dt_isr_handler(void* arg)
{
    BaseType_t taskAwoken = pdFALSE;

    dt_val = true;
    xSemaphoreGiveFromISR(sem_ky040, &taskAwoken);
    portYIELD_FROM_ISR(taskAwoken); //wake up the task waiting for semaphore
}

static void IRAM_ATTR ky040_clk_isr_handler(void* arg)
{
    BaseType_t taskAwoken = pdFALSE;

    clk_val = true;
    xSemaphoreGiveFromISR(sem_ky040, &taskAwoken);
    portYIELD_FROM_ISR(taskAwoken); //wake up the task waiting for semaphore
}

static esp_err_t init_ky040() {

    esp_err_t err;

    if (sem_ky040 != NULL) {
        log_msg(TAG, "KY-040 already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    sem_ky040 = xSemaphoreCreateBinary();
    if (sem_ky040 == NULL) {
        log_msg(TAG, "Error creating semaphore");
        return ESP_ERR_NOT_ALLOWED;
    }

    err = setup_pin(SW_PIN_KY040);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setup pin %d", esp_err_to_name(err), SW_PIN_KY040);
        return err;
    }
    err = setup_pin(DT_PIN_KY040);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setup pin %d", esp_err_to_name(err), DT_PIN_KY040);
        return err;
    }
    err = setup_pin(CLK_PIN_KY040);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setup pin %d", esp_err_to_name(err), CLK_PIN_KY040);
        return err;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        log_msg(TAG, "Error (%s) installing isr pin %d", esp_err_to_name(err), SW_PIN_KY040);
        return err;
    }

    err = gpio_isr_handler_add(SW_PIN_KY040, ky040_sw_isr_handler, NULL);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding isr handler pin %d", esp_err_to_name(err), SW_PIN_KY040);
        return err;
    }
    err = gpio_isr_handler_add(DT_PIN_KY040, ky040_dt_isr_handler, NULL);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding isr handler pin %d", esp_err_to_name(err), DT_PIN_KY040);
        return err;
    }
    err = gpio_isr_handler_add(CLK_PIN_KY040, ky040_clk_isr_handler, NULL);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding isr handler pin %d", esp_err_to_name(err), CLK_PIN_KY040);
        return err;
    }

    log_msg(TAG, "KY-040 initialized");
    return ESP_OK;
}

static void ky040_task(void * params) {
    esp_err_t err;
    
    err = init_ky040();
    if (err != ESP_OK){
        vTaskDelete(NULL);
        return;
    }

    bool sw_button = false;

    int8_t factor = 0;

    int8_t val = -1;

#if CONFIG_USE_LEDLIB
    int8_t percent = 0;
#endif

    while (monitoring)
    {
        if (xSemaphoreTake(sem_ky040, portMAX_DELAY) == pdTRUE) {

            if (!clk_val && !dt_val) {
                //if sw button clicked
                sw_button = gpio_get_level(SW_PIN_KY040);
                //log_msg(TAG, "button value : %d", !sw_button);
                val = sw_button;
            } else if (!clk_val && dt_val) {
                //if dt before clk, turn left
                factor = -1;
            } else if (clk_val && !dt_val) {
                //if clk before dt, turn right
                factor = 1;
            } else {
                // both equal true -> clk and dt ok, trigger activate event
                //log_msg(TAG, "turn %s", factor == 1 ? "right" : "left");
                val = factor == 1 ? 3 : 2;
                dt_val = false;
                clk_val = false;
            }
    
            if (val != -1) {

            #if CONFIG_USE_LEDLIB
                if (val > 1) {
                    percent += factor;
                    if (percent < 0) percent = 0;
                    else if (percent > 100) percent = 100;
                    else ledc_buzzer(percent);
                }
            #endif
                header_sensor_t header = {0};
                header.esp_id = (uint8_t)CONFIG_ESP_ID;
                header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
                header.type = SENSOR_TYPE_KY040;
                uint8_t buf[HEADER_SENSOR_SIZE + 1];
                serialize_header(&header, buf);
                buf[HEADER_SENSOR_SIZE] = val;
                log_msg(TAG, "send val : %d", val);
            #if CONFIG_USE_UDPLIB
                send_udp_sensor(buf, sizeof(buf));
            #endif
            #if CONFIG_USE_ESPNOW
            #endif
                val = -1;
            }
        }
    }
    vTaskDelete(NULL);
}

#endif

#if CONFIG_USE_DHT11

#define OUT_PIN_DHT11 23

static rmt_channel_handle_t dht11_rx_chan = NULL;
static SemaphoreHandle_t dht11_rx_sem = NULL;
static rmt_symbol_word_t dht11_rx_buffer[64];

// Callback IRAM exécuté automatiquement par le RMT quand les 40 bits sont stockés
static bool IRAM_ATTR dht11_rmt_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    // On débloque la tâche principale immédiatement
    xSemaphoreGiveFromISR(dht11_rx_sem, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

static esp_err_t init_dht11() {
    esp_err_t err;

    // 1. Création du sémaphore de synchronisation
    dht11_rx_sem = xSemaphoreCreateBinary();
    if (dht11_rx_sem == NULL) {
        log_msg(TAG, "Error creating DHT11 semaphore");
        return ESP_ERR_NO_MEM;
    }

    // 2. Configuration du canal de réception RMT
    rmt_rx_channel_config_t rx_config = {
        .gpio_num = OUT_PIN_DHT11,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000, // 1 tick = 1 microseconde (idéal pour le DHT)
        .mem_block_symbols = 64,  // Assez grand pour stocker les 40 bits + handshake
    };

    err = rmt_new_rx_channel(&rx_config, &dht11_rx_chan);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) creating RMT RX channel", esp_err_to_name(err));
        return err;
    }

    // 3. Enregistrement du callback de fin de réception
    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = dht11_rmt_rx_done_callback,
    };

    err = rmt_rx_register_event_callbacks(dht11_rx_chan, &cbs, NULL);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) registering RMT callbacks", esp_err_to_name(err));
        return err;
    }

    // 4. Activation du périphérique RMT
    err = rmt_enable(dht11_rx_chan);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) enabling RMT channel", esp_err_to_name(err));
        return err;
    }

    log_msg(TAG, "DHT11 RMT initialized");
    return ESP_OK;
}

static void dht11_task(void * params) {
    esp_err_t err;
    
    err = init_dht11();
    if (err != ESP_OK){
        vTaskDelete(NULL);
        return;
    }

    while (monitoring)
    {
        // --- 1. SÉQUENCE DE RÉVEIL DU CAPTEUR (START SIGNAL) ---
        gpio_set_direction(OUT_PIN_DHT11, GPIO_MODE_OUTPUT_OD);
        gpio_set_level(OUT_PIN_DHT11, 0);
        vTaskDelay(pdMS_TO_TICKS(20)); // Maintien à 0 pendant 20ms

        // ON PASSE EN ENTRÉE ET ON ARME LE RMT TOUT DE SUITE (Pas de délai us !)
        gpio_set_direction(OUT_PIN_DHT11, GPIO_MODE_INPUT);

        // --- 2. CAPTURE MATÉRIELLE VIA RMT ---
        rmt_receive_config_t receive_config = {
            .signal_range_min_ns = 1000,   // Filtre anti-bruit corrigé (< 3187 ns)
            .signal_range_max_ns = 150000, // Timeout à 150us
        };
        
        rmt_receive(dht11_rx_chan, dht11_rx_buffer, sizeof(dht11_rx_buffer), &receive_config);

        // Attente du callback RMT (ISR) via le sémaphore
        if (xSemaphoreTake(dht11_rx_sem, pdMS_TO_TICKS(100)) == pdTRUE) 
        {
            uint8_t data[5] = {0};

            // --- 3. ALIGNEMENT DYNAMIQUE DES BITS ---
            // Si duration0 (le premier état bas) est > 65µs, c'est le handshake initial de 80µs.
            // Donc les données utiles commencent à l'index 1. Sinon, on commence à 0.
            int start_index = (dht11_rx_buffer[0].duration0 > 65) ? 1 : 0;

            // --- 4. DÉCODAGE DES 40 BITS ---
            for (int i = 0; i < 40; i++) {
                uint32_t low_dur  = dht11_rx_buffer[start_index + i].duration0;
                uint32_t high_dur = dht11_rx_buffer[start_index + i].duration1;

                if (high_dur > low_dur) {
                    data[i / 8] |= (1 << (7 - (i % 8)));
                }
            }

            // --- 5. VÉRIFICATION ET EXPÉDITION ---
            if (data[4] == ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) 
            {
                uint8_t humidity = data[0];
                uint8_t temperature = data[2];

                header_sensor_t header = {0};
                header.esp_id = (uint8_t)CONFIG_ESP_ID;
                header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
                header.type = SENSOR_TYPE_DHT11; 
                
                uint8_t buf[HEADER_SENSOR_SIZE + 2];
                serialize_header(&header, buf);
                buf[HEADER_SENSOR_SIZE]     = humidity;
                buf[HEADER_SENSOR_SIZE + 1] = temperature;

                log_msg(TAG, "Humidity: %u%%, Temp: %u°C", humidity, temperature);

            #if CONFIG_USE_UDPLIB
                send_udp_sensor(buf, sizeof(buf));
            #endif
            #if CONFIG_USE_ESPNOW
            #endif
            } 
            else {
                // Optionnel : Décommente la ligne dessous pour calibrer tes temps si ça persiste
                // log_msg(TAG, "B0_low: %ld, B0_high: %ld, StartIdx: %d", dht11_rx_buffer[0].duration0, dht11_rx_buffer[0].duration1, start_index);
                log_msg(TAG, "DHT11 Checksum failed, invalid data");
            }
        } 
        else {
            log_msg(TAG, "DHT11 RMT Response Timeout");
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000)); // Repos de 2 secondes obligatoire
    }
    vTaskDelete(NULL);
}

#endif

#if CONFIG_USE_KY020

#define OUT_PIN_KY020 23

static SemaphoreHandle_t sem_ky020 = NULL;

static void IRAM_ATTR ky020_isr_handler(void* arg)
{
    BaseType_t taskAwoken = pdFALSE;

    xSemaphoreGiveFromISR(sem_ky020, &taskAwoken);
    portYIELD_FROM_ISR(taskAwoken); //wake up the task waiting for semaphore
}

static esp_err_t init_ky020() {

    esp_err_t err;

    if (sem_ky020 != NULL) {
        log_msg(TAG, "KY-040 already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    sem_ky020 = xSemaphoreCreateBinary();
    if (sem_ky020 == NULL) {
        log_msg(TAG, "Error creating semaphore");
        return ESP_ERR_NOT_ALLOWED;
    }

    err = gpio_reset_pin(OUT_PIN_KY020);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), OUT_PIN_KY020);
        return err;
    }

    err = gpio_set_direction(OUT_PIN_KY020, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction pin %d", esp_err_to_name(err), OUT_PIN_KY020);
        return err;
    }

    err = gpio_set_intr_type(OUT_PIN_KY020, GPIO_INTR_ANYEDGE);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting interrupt type pin %d", esp_err_to_name(err), OUT_PIN_KY020);
        return err;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        log_msg(TAG, "Error (%s) installing isr pin %d", esp_err_to_name(err), OUT_PIN_KY020);
        return err;
    }

    err = gpio_isr_handler_add(OUT_PIN_KY020, ky020_isr_handler, NULL);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding isr handler pin %d", esp_err_to_name(err), OUT_PIN_KY020);
        return err;
    }

    log_msg(TAG, "KY-020 initialized");
    return ESP_OK;
}

static void ky020_task(void * params) {
    esp_err_t err;
    
    err = init_ky020();
    if (err != ESP_OK){
        vTaskDelete(NULL);
        return;
    }

    while (monitoring)
    {
        if (xSemaphoreTake(sem_ky020, portMAX_DELAY) == pdTRUE) {

            bool val = gpio_get_level(OUT_PIN_KY020);

            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_KY020;
            uint8_t buf[HEADER_SENSOR_SIZE + 1];
            serialize_header(&header, buf);
            buf[HEADER_SENSOR_SIZE] = val;
            log_msg(TAG, "send val : %d", val);
        #if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
        #endif
        #if CONFIG_USE_ESPNOW
        #endif

        }
    }
    vTaskDelete(NULL);
}

#endif

#if CONFIG_USE_KY018

// Variables globales pour le driver
static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t adc1_cali_handle = NULL;

#define KY018_ADC_CHANNEL ADC_CHANNEL_4

/**
 * Initialize KY
 */
esp_err_t init_ky018() {

    esp_err_t err;

    // 1. Init unité ADC1
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    err = adc_oneshot_new_unit(&init_config, &adc1_handle);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_0  // 0~3.1V
    };
    err = adc_oneshot_config_channel(adc1_handle, KY018_ADC_CHANNEL, &chan_config);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(err));
        return err;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_0,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_config, &adc1_cali_handle);
#else
    // 3. Calibration (line fitting, ESP32 classique)
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_0,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_line_fitting(&cali_config, &adc1_cali_handle);
#endif

    if (err != ESP_OK) {
        // Pas bloquant, on continue sans calibration
        log_msg_lvl(ESP_LOG_WARN, TAG, "Calibration non disponible: %s", esp_err_to_name(err));
        adc1_cali_handle = NULL;
    }

    log_msg(TAG, "KY-018 initialized");

    return ESP_OK;
}

/**
 * Periodic KY-018 monitoring task
 */
static void ky018_task(void * params) {

    int raw_val = 0;
    esp_err_t err;

    err = init_ky018();
    if (err != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    while (monitoring) {
        esp_err_t err = adc_oneshot_read(adc1_handle, KY018_ADC_CHANNEL, &raw_val);
        if (err == ESP_OK) {

            int32_t val = raw_val;
            log_msg(TAG, "raw value: %d", val);
                    
            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_KY018;
            uint8_t buf[HEADER_SENSOR_SIZE + 4];
            serialize_header(&header, buf);
            memcpy(&buf[HEADER_SENSOR_SIZE], &val, 4);
            
        #if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
        #endif
        #if CONFIG_USE_ESPNOW
        #endif
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelete(NULL);
}

#endif

#if CONFIG_USE_KY031

#define OUT_PIN_KY031 23

static SemaphoreHandle_t sem_ky031 = NULL;

static void IRAM_ATTR ky031_isr_handler(void* arg)
{
    BaseType_t taskAwoken = pdFALSE;

    xSemaphoreGiveFromISR(sem_ky031, &taskAwoken);
    portYIELD_FROM_ISR(taskAwoken); //wake up the task waiting for semaphore
}

static esp_err_t init_ky031() {

    esp_err_t err;

    if (sem_ky031 != NULL) {
        log_msg(TAG, "KY-031 already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    sem_ky031 = xSemaphoreCreateBinary();
    if (sem_ky031 == NULL) {
        log_msg(TAG, "Error creating semaphore");
        return ESP_ERR_NOT_ALLOWED;
    }

    err = gpio_reset_pin(OUT_PIN_KY031);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), OUT_PIN_KY031);
        return err;
    }

    err = gpio_set_direction(OUT_PIN_KY031, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction pin %d", esp_err_to_name(err), OUT_PIN_KY031);
        return err;
    }

    err = gpio_set_intr_type(OUT_PIN_KY031, GPIO_INTR_POSEDGE);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting interrupt type pin %d", esp_err_to_name(err), OUT_PIN_KY031);
        return err;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        log_msg(TAG, "Error (%s) installing isr pin %d", esp_err_to_name(err), OUT_PIN_KY031);
        return err;
    }

    err = gpio_isr_handler_add(OUT_PIN_KY031, ky031_isr_handler, NULL);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding isr handler pin %d", esp_err_to_name(err), OUT_PIN_KY031);
        return err;
    }

    log_msg(TAG, "KY-031 initialized");
    return ESP_OK;
}

static void ky031_task(void * params) {
    esp_err_t err;
    
    err = init_ky031();
    if (err != ESP_OK){
        vTaskDelete(NULL);
        return;
    }

    while (monitoring)
    {
        if (xSemaphoreTake(sem_ky031, portMAX_DELAY) == pdTRUE) {

            bool val = gpio_get_level(OUT_PIN_KY031);

            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_KY031;
            uint8_t buf[HEADER_SENSOR_SIZE + 1];
            serialize_header(&header, buf);
            buf[HEADER_SENSOR_SIZE] = val;
            log_msg(TAG, "send val : %d", val);
        #if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
        #endif
        #if CONFIG_USE_ESPNOW
        #endif

        }
    }
    vTaskDelete(NULL);
}

#endif

#if CONFIG_USE_KY017

#define OUT_PIN_KY017 23

static SemaphoreHandle_t sem_ky017 = NULL;

static void IRAM_ATTR ky017_isr_handler(void* arg)
{
    BaseType_t taskAwoken = pdFALSE;

    xSemaphoreGiveFromISR(sem_ky017, &taskAwoken);
    portYIELD_FROM_ISR(taskAwoken); //wake up the task waiting for semaphore
}

static esp_err_t init_ky017() {

    esp_err_t err;

    if (sem_ky017 != NULL) {
        log_msg(TAG, "KY-017 already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    sem_ky017 = xSemaphoreCreateBinary();
    if (sem_ky017 == NULL) {
        log_msg(TAG, "Error creating semaphore");
        return ESP_ERR_NOT_ALLOWED;
    }

    err = gpio_reset_pin(OUT_PIN_KY017);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), OUT_PIN_KY017);
        return err;
    }

    err = gpio_set_direction(OUT_PIN_KY017, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction pin %d", esp_err_to_name(err), OUT_PIN_KY017);
        return err;
    }

    err = gpio_set_intr_type(OUT_PIN_KY017, GPIO_INTR_ANYEDGE);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting interrupt type pin %d", esp_err_to_name(err), OUT_PIN_KY017);
        return err;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        log_msg(TAG, "Error (%s) installing isr pin %d", esp_err_to_name(err), OUT_PIN_KY017);
        return err;
    }

    err = gpio_isr_handler_add(OUT_PIN_KY017, ky017_isr_handler, NULL);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding isr handler pin %d", esp_err_to_name(err), OUT_PIN_KY017);
        return err;
    }

    log_msg(TAG, "KY-017 initialized");
    return ESP_OK;
}

static void ky017_task(void * params) {
    esp_err_t err;
    
    err = init_ky017();
    if (err != ESP_OK){
        vTaskDelete(NULL);
        return;
    }

    while (monitoring)
    {
        if (xSemaphoreTake(sem_ky017, portMAX_DELAY) == pdTRUE) {

            bool val = gpio_get_level(OUT_PIN_KY017);

            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_KY017;
            uint8_t buf[HEADER_SENSOR_SIZE + 1];
            serialize_header(&header, buf);
            buf[HEADER_SENSOR_SIZE] = val;
            log_msg(TAG, "send val : %d", val);
        #if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
        #endif
        #if CONFIG_USE_ESPNOW
        #endif

        }
    }
    vTaskDelete(NULL);
}

#endif

#if CONFIG_USE_KY005

#define IR_TX_GPIO_PIN 23

#define NEC_HEADER_SYMBOL    (rmt_symbol_word_t){ .duration0 = 9000, .level0 = 1, .duration1 = 4500, .level1 = 0 }
#define NEC_BIT_ONE_SYMBOL   (rmt_symbol_word_t){ .duration0 = 562,  .level0 = 1, .duration1 = 1688, .level1 = 0 }
#define NEC_BIT_ZERO_SYMBOL  (rmt_symbol_word_t){ .duration0 = 562,  .level0 = 1, .duration1 = 562,  .level1 = 0 }
#define NEC_END_SYMBOL       (rmt_symbol_word_t){ .duration0 = 562,  .level0 = 1, .duration1 = 0,    .level1 = 0 }

static rmt_channel_handle_t tx_ky005_channel = NULL;
static rmt_encoder_handle_t copy_encoder_ky005 = NULL;

static esp_err_t init_ky005() {
    
    esp_err_t err;

    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = IR_TX_GPIO_PIN,         // Ton GPIO 23
        .mem_block_symbols = 64,
        .resolution_hz = 1000000,           // 1 MHz
        .trans_queue_depth = 4,             // LE CHAMP MANQUANT CRUCIAL !
        .flags.invert_out = false,
        .flags.with_dma = false,
    };
    err = rmt_new_tx_channel(&tx_chan_config, &tx_ky005_channel);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) creating tx channel rmt", esp_err_to_name(err));
        return err;
    }

    // Modulation 38 kHz indispensable pour le récepteur KY-022
    rmt_carrier_config_t carrier_config = {
        .frequency_hz = 38000,
        .duty_cycle = 0.33, // Allumé 33% du temps pour économiser la LED et maximiser la portée
    };
    err = rmt_apply_carrier(tx_ky005_channel, &carrier_config);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) apply carrier modulation rmt", esp_err_to_name(err));
        return err;
    }
    
    err = rmt_enable(tx_ky005_channel);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) enable rmt", esp_err_to_name(err));
        return err;
    }

    // AJOUT DE L'ENCODEUR DE COPIE NATIF
    rmt_copy_encoder_config_t copy_encoder_config = {};
    err = rmt_new_copy_encoder(&copy_encoder_config, &copy_encoder_ky005);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) creating copy encoder", esp_err_to_name(err));
        return err;
    }
    
    log_msg(TAG, "KY-005 initialized");
    return ESP_OK;
}

static esp_err_t send_ir_frame_raw(uint32_t data) {
    // 1 header + 32 bits de données + 1 bit de fin = 34 symboles RMT
    rmt_symbol_word_t ir_payload[34];
    
    // Étape 1 : On injecte le Header NEC
    ir_payload[0] = NEC_HEADER_SYMBOL;

    // Étape 2 : On convertit les 32 bits de données un par un (MSB first)
    for (int i = 0; i < 32; i++) {
        if ((data >> (31 - i)) & 0x01) {
            ir_payload[i + 1] = NEC_BIT_ONE_SYMBOL;
        } else {
            ir_payload[i + 1] = NEC_BIT_ZERO_SYMBOL;
        }
    }

    // Étape 3 : On ferme la trame
    ir_payload[33] = NEC_END_SYMBOL;

    rmt_transmit_config_t transmit_config = {
        .loop_count = 0, // Envoyer une seule fois
    };

    // On balance la sauce au matériel
    return rmt_transmit(tx_ky005_channel, copy_encoder_ky005, 
        (const void *)ir_payload, sizeof(ir_payload), &transmit_config);
    
}

static void ky005_task(void * params) {
    esp_err_t err;

    err = init_ky005();
    if (err != ESP_OK){
        vTaskDelete(NULL);
        return;
    }

    uint32_t count = 0;

    while (monitoring) {

        err = send_ir_frame_raw(count);
        
        if (err == ESP_OK) {
            log_msg(TAG, "Sending IR count : %u", count);
            count++;
        } else {
            log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) sending IR frame", esp_err_to_name(err));
        }

        // Délai de 1 seconde (1000 ms) avant le prochain envoi
        vTaskDelay(pdMS_TO_TICKS(1000));

    }
    vTaskDelete(NULL);

}

#endif

#if CONFIG_USE_KY022

#define IR_RX_GPIO_PIN 22

static rmt_channel_handle_t rx_ky022_channel = NULL;
static SemaphoreHandle_t ky022_rx_sem = NULL;
static rmt_symbol_word_t ky022_rx_buffer[64];

// Le callback de l'interruption (ISR) quand le RMT a fini de capter la trame
static bool ky022_rmt_rx_done_callback(rmt_channel_handle_t rx_chan, const rmt_rx_done_event_data_t *edata, void *user_data) {
    BaseType_t high_task_wakeup = pdFALSE;
    // On libère la tâche qui attend sagement
    xSemaphoreGiveFromISR(ky022_rx_sem, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}


static esp_err_t init_ky022() {
    esp_err_t err;

    // 1. Création du sémaphore
    ky022_rx_sem = xSemaphoreCreateBinary();
    if (ky022_rx_sem == NULL) {
        return ESP_ERR_NO_MEM;
    }

    rmt_rx_channel_config_t rx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000, // 1 tick = 1 microseconde
        .mem_block_symbols = 64,  // Assez grand pour stocker nos 34 symboles
        .gpio_num = IR_RX_GPIO_PIN,
        .flags.with_dma = false,
    };
    
    err = rmt_new_rx_channel(&rx_chan_config, &rx_ky022_channel);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) creating rx channel rmt", esp_err_to_name(err));
        return err;
    }

    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = ky022_rmt_rx_done_callback,
    };

    err = rmt_rx_register_event_callbacks(rx_ky022_channel, &cbs, NULL);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) register rx callback rmt", esp_err_to_name(err));
        return err;
    }
    
    // On active le canal RX
    err = rmt_enable(rx_ky022_channel);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) enable rx channel rmt", esp_err_to_name(err));
        return err;
    }

    log_msg(TAG, "KY-022 initialized");
    return ESP_OK;
}

static void ky022_task(void *params) {
    esp_err_t err;
    
    if (init_ky022() != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    rmt_receive_config_t receive_config = {
        .signal_range_min_ns = 1000,      // Optionnel : ignore les impulsions < 1µs (bruit)
        .signal_range_max_ns = 20000000,  // Si rien ne se passe pendant 20ms, on valide la fin de la trame
    };

    while (monitoring) {

        memset(ky022_rx_buffer, 0, sizeof(ky022_rx_buffer));
        
        // Cette fonction bloque la tâche jusqu'à ce qu'une trame IR arrive
        rmt_receive(rx_ky022_channel, ky022_rx_buffer, sizeof(ky022_rx_buffer), &receive_config);

        if (xSemaphoreTake(ky022_rx_sem, portMAX_DELAY) == pdTRUE) {
            
            // On compte combien de symboles le RMT a écrit dans le buffer
            size_t symbols_count = 0;
            for (size_t i = 0; i < 64; i++) {
                if (ky022_rx_buffer[i].duration0 == 0 && ky022_rx_buffer[i].duration1 == 0) {
                    symbols_count = i;
                    break;
                }
            }

            // Une trame NEC complète = 34 symboles
            if (symbols_count >= 34) {
                uint32_t ir_data = 0;

                // Décodage des 32 bits (on saute l'index 0 qui est le Header)
                for (int i = 0; i < 32; i++) {
                    // Dans le protocole NEC, duration1 (le silence) fait 562µs pour un '0' et 1688µs pour un '1'
                    if (ky022_rx_buffer[i + 1].duration1 > 1100) {
                        ir_data |= (1 << (31 - i)); // Bit à 1
                    }
                }

                log_msg(TAG, "IR frame received : 0x%08X", ir_data);
            } else {
                log_msg_lvl(ESP_LOG_WARN, TAG, "Signal received, invalid frame (symbols: %d)", symbols_count);
            }
        }
        
        // Petit repos optionnel de 100ms entre deux décodages pour ne pas spammer si le bouton reste appuyé
        vTaskDelay(pdMS_TO_TICKS(100));
        
    }
    vTaskDelete(NULL);
}

#endif


#if CONFIG_USE_KY021

#define OUT_PIN_KY021 23

static SemaphoreHandle_t sem_ky021 = NULL;

static void IRAM_ATTR ky021_isr_handler(void* arg)
{
    BaseType_t taskAwoken = pdFALSE;

    xSemaphoreGiveFromISR(sem_ky021, &taskAwoken);
    portYIELD_FROM_ISR(taskAwoken); //wake up the task waiting for semaphore
}

static esp_err_t init_ky021() {

    esp_err_t err;

    if (sem_ky021 != NULL) {
        log_msg(TAG, "KY-021 already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    sem_ky021 = xSemaphoreCreateBinary();
    if (sem_ky021 == NULL) {
        log_msg(TAG, "Error creating semaphore");
        return ESP_ERR_NOT_ALLOWED;
    }

    err = gpio_reset_pin(OUT_PIN_KY021);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), OUT_PIN_KY021);
        return err;
    }

    err = gpio_set_direction(OUT_PIN_KY021, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction pin %d", esp_err_to_name(err), OUT_PIN_KY021);
        return err;
    }

    err = gpio_set_intr_type(OUT_PIN_KY021, GPIO_INTR_ANYEDGE);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting interrupt type pin %d", esp_err_to_name(err), OUT_PIN_KY021);
        return err;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        log_msg(TAG, "Error (%s) installing isr pin %d", esp_err_to_name(err), OUT_PIN_KY021);
        return err;
    }

    err = gpio_isr_handler_add(OUT_PIN_KY021, ky021_isr_handler, NULL);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding isr handler pin %d", esp_err_to_name(err), OUT_PIN_KY021);
        return err;
    }

    log_msg(TAG, "KY-021 initialized");
    return ESP_OK;
}

static void ky021_task(void * params) {
    esp_err_t err;
    
    err = init_ky021();
    if (err != ESP_OK){
        vTaskDelete(NULL);
        return;
    }

    while (monitoring)
    {
        if (xSemaphoreTake(sem_ky021, portMAX_DELAY) == pdTRUE) {

            bool val = gpio_get_level(OUT_PIN_KY021);

            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_KY021;
            uint8_t buf[HEADER_SENSOR_SIZE + 1];
            serialize_header(&header, buf);
            buf[HEADER_SENSOR_SIZE] = val;
            log_msg(TAG, "send val : %d", val);
        #if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
        #endif
        #if CONFIG_USE_ESPNOW
        #endif

        }
    }
    vTaskDelete(NULL);
}

#endif

#if CONFIG_USE_KY004

#define OUT_PIN_KY004 23

static SemaphoreHandle_t sem_ky004 = NULL;

static void IRAM_ATTR ky004_isr_handler(void* arg)
{
    BaseType_t taskAwoken = pdFALSE;

    xSemaphoreGiveFromISR(sem_ky004, &taskAwoken);
    portYIELD_FROM_ISR(taskAwoken); //wake up the task waiting for semaphore
}

static esp_err_t init_ky004() {

    esp_err_t err;

    if (sem_ky004 != NULL) {
        log_msg(TAG, "KY-004 already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    sem_ky004 = xSemaphoreCreateBinary();
    if (sem_ky004 == NULL) {
        log_msg(TAG, "Error creating semaphore");
        return ESP_ERR_NOT_ALLOWED;
    }

    err = gpio_reset_pin(OUT_PIN_KY004);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), OUT_PIN_KY004);
        return err;
    }

    err = gpio_set_direction(OUT_PIN_KY004, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction pin %d", esp_err_to_name(err), OUT_PIN_KY004);
        return err;
    }

    err = gpio_set_intr_type(OUT_PIN_KY004, GPIO_INTR_ANYEDGE);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting interrupt type pin %d", esp_err_to_name(err), OUT_PIN_KY004);
        return err;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        log_msg(TAG, "Error (%s) installing isr pin %d", esp_err_to_name(err), OUT_PIN_KY004);
        return err;
    }

    err = gpio_isr_handler_add(OUT_PIN_KY004, ky004_isr_handler, NULL);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding isr handler pin %d", esp_err_to_name(err), OUT_PIN_KY004);
        return err;
    }

    log_msg(TAG, "KY-004 initialized");
    return ESP_OK;
}

static void ky004_task(void * params) {
    esp_err_t err;
    
    err = init_ky004();
    if (err != ESP_OK){
        vTaskDelete(NULL);
        return;
    }

    while (monitoring)
    {
        if (xSemaphoreTake(sem_ky004, portMAX_DELAY) == pdTRUE) {

            bool val = gpio_get_level(OUT_PIN_KY004);

            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_KY004;
            uint8_t buf[HEADER_SENSOR_SIZE + 1];
            serialize_header(&header, buf);
            buf[HEADER_SENSOR_SIZE] = val;
            log_msg(TAG, "send val : %d", val);
        #if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
        #endif
        #if CONFIG_USE_ESPNOW
        #endif

        }
    }
    vTaskDelete(NULL);
}
#endif


#if CONFIG_USE_KY039

// Variables globales pour le driver
static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t adc1_cali_handle = NULL;

#define KY039_ADC_CHANNEL ADC_CHANNEL_4 //gpio32

/**
 * Initialize KY
 */
esp_err_t init_ky039() {

    esp_err_t err;

    // 1. Init unité ADC1
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    err = adc_oneshot_new_unit(&init_config, &adc1_handle);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12  // 0~3.1V
    };
    err = adc_oneshot_config_channel(adc1_handle, KY039_ADC_CHANNEL, &chan_config);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(err));
        return err;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_config, &adc1_cali_handle);
#else
    // 3. Calibration (line fitting, ESP32 classique)
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_line_fitting(&cali_config, &adc1_cali_handle);
#endif

    if (err != ESP_OK) {
        // Pas bloquant, on continue sans calibration
        log_msg_lvl(ESP_LOG_WARN, TAG, "Calibration non disponible: %s", esp_err_to_name(err));
        adc1_cali_handle = NULL;
    }

    log_msg(TAG, "KY-039 initialized");

    return ESP_OK;
}

/**
 * Periodic KY-039 monitoring task
 */
static void ky039_task(void * params) {

    int raw_val = 0;
    esp_err_t err;

    err = init_ky039();
    if (err != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    while (monitoring) {
        esp_err_t err = adc_oneshot_read(adc1_handle, KY039_ADC_CHANNEL, &raw_val);
        if (err == ESP_OK) {

            int32_t val = raw_val;
            log_msg(TAG, "raw value: %d", val);
                    
            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_KY039;
            uint8_t buf[HEADER_SENSOR_SIZE + 4];
            serialize_header(&header, buf);
            memcpy(&buf[HEADER_SENSOR_SIZE], &val, 4);
            
        #if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
        #endif
        #if CONFIG_USE_ESPNOW
        #endif
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelete(NULL);
}

#endif


#if CONFIG_USE_KY032

#define OUT_PIN_KY032 23

static SemaphoreHandle_t sem_ky032 = NULL;

static void IRAM_ATTR ky032_isr_handler(void* arg)
{
    BaseType_t taskAwoken = pdFALSE;

    xSemaphoreGiveFromISR(sem_ky032, &taskAwoken);
    portYIELD_FROM_ISR(taskAwoken); //wake up the task waiting for semaphore
}

static esp_err_t init_ky032() {

    esp_err_t err;

    if (sem_ky032 != NULL) {
        log_msg(TAG, "KY-031 already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    sem_ky032 = xSemaphoreCreateBinary();
    if (sem_ky032 == NULL) {
        log_msg(TAG, "Error creating semaphore");
        return ESP_ERR_NOT_ALLOWED;
    }

    err = gpio_reset_pin(OUT_PIN_KY032);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), OUT_PIN_KY032);
        return err;
    }

    err = gpio_set_direction(OUT_PIN_KY032, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction pin %d", esp_err_to_name(err), OUT_PIN_KY032);
        return err;
    }

    err = gpio_set_intr_type(OUT_PIN_KY032, GPIO_INTR_POSEDGE);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting interrupt type pin %d", esp_err_to_name(err), OUT_PIN_KY032);
        return err;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        log_msg(TAG, "Error (%s) installing isr pin %d", esp_err_to_name(err), OUT_PIN_KY032);
        return err;
    }

    err = gpio_isr_handler_add(OUT_PIN_KY032, ky032_isr_handler, NULL);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding isr handler pin %d", esp_err_to_name(err), OUT_PIN_KY032);
        return err;
    }

    log_msg(TAG, "KY-031 initialized");
    return ESP_OK;
}

static void ky032_task(void * params) {
    esp_err_t err;
    
    err = init_ky032();
    if (err != ESP_OK){
        vTaskDelete(NULL);
        return;
    }

    while (monitoring)
    {
        if (xSemaphoreTake(sem_ky032, portMAX_DELAY) == pdTRUE) {

            bool val = gpio_get_level(OUT_PIN_KY032);

            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_KY032;
            uint8_t buf[HEADER_SENSOR_SIZE + 1];
            serialize_header(&header, buf);
            buf[HEADER_SENSOR_SIZE] = val;
            log_msg(TAG, "send val : %d", val);
        #if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
        #endif
        #if CONFIG_USE_ESPNOW
        #endif

        }
    }
    vTaskDelete(NULL);
}

#endif


#if CONFIG_USE_KY023

#define SW_PIN_KY023 22
#define KY023_ADC_CHANNEL_X ADC_CHANNEL_4 //gpio32
#define KY023_ADC_CHANNEL_Y ADC_CHANNEL_5 //gpio33

// On demande 2000 Hz au total. Comme on a 2 canaux (X et Y), 
// chaque canal sera échantillonné à 1000 Hz (pile 1 ms !)
#define SAMPLE_FREQ_HZ     40000  // 40 kHz : compatible avec absolument tous les modèles d'ESP32
#define READ_LEN           1024   // Taille du buffer DMA (32 mesures de 2 octets)

// Variables globales pour le driver
static adc_continuous_handle_t adc_handle = NULL;

static SemaphoreHandle_t sem_ky023 = NULL;

static void IRAM_ATTR ky023_sw_isr_handler(void* arg)
{
    BaseType_t taskAwoken = pdFALSE;
    
    xSemaphoreGiveFromISR(sem_ky023, &taskAwoken);
    portYIELD_FROM_ISR(taskAwoken); //wake up the task waiting for semaphore
}

static esp_err_t init_ky023() {

    esp_err_t err;

    if (sem_ky023 != NULL) {
        log_msg(TAG, "KY-023 already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    sem_ky023 = xSemaphoreCreateBinary();
    if (sem_ky023 == NULL) {
        log_msg(TAG, "Error creating semaphore");
        return ESP_ERR_NOT_ALLOWED;
    }
    
    err = gpio_reset_pin(SW_PIN_KY023);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), SW_PIN_KY023);
        return err;
    }

    err = gpio_set_direction(SW_PIN_KY023, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction pin %d", esp_err_to_name(err), SW_PIN_KY023);
        return err;
    }

    err = gpio_set_intr_type(SW_PIN_KY023, GPIO_INTR_ANYEDGE);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting interrupt type pin %d", esp_err_to_name(err), SW_PIN_KY023);
        return err;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        log_msg(TAG, "Error (%s) installing isr pin %d", esp_err_to_name(err), SW_PIN_KY023);
        return err;
    }

    err = gpio_isr_handler_add(SW_PIN_KY023, ky023_sw_isr_handler, NULL);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding isr handler pin %d", esp_err_to_name(err), SW_PIN_KY023);
        return err;
    }

   // --- CONFIGURATION DE L'ADC CONTINU (DMA) ---
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 4096,
        .conv_frame_size = READ_LEN,
    };
    err = adc_continuous_new_handle(&adc_config, &adc_handle);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) while creating ADC continuous unit", esp_err_to_name(err));
        return err;
    }

    // On définit le paterne de scan : Canal X puis Canal Y
    adc_digi_pattern_config_t adc_pattern[2] = {
        {
            .atten = ADC_ATTEN_DB_12,
            .channel = KY023_ADC_CHANNEL_X,
            .unit = ADC_UNIT_1,
            .bit_width = ADC_BITWIDTH_12,
        },
        {
            .atten = ADC_ATTEN_DB_12,
            .channel = KY023_ADC_CHANNEL_Y,
            .unit = ADC_UNIT_1,
            .bit_width = ADC_BITWIDTH_12,
        }
    };

    adc_continuous_config_t dig_cfg = {
        .pattern_num = 2,
        .adc_pattern = adc_pattern,
        .sample_freq_hz = SAMPLE_FREQ_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
    };

    err = adc_continuous_config(adc_handle, &dig_cfg);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) while applying adc config", esp_err_to_name(err));
        return err;
    }

    err = adc_continuous_start(adc_handle);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) starting continuous adc ", esp_err_to_name(err));
        return err;
    }

    log_msg(TAG, "KY-023 initialized");
    return ESP_OK;
}

static void ky023_task_sw(void * params) {
    esp_err_t err;
    
    uint8_t val;

    while (monitoring)
    {
        if (xSemaphoreTake(sem_ky023, portMAX_DELAY) == pdTRUE) {

            val = gpio_get_level(SW_PIN_KY023);

            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_KY023_SW;
            uint8_t buf[HEADER_SENSOR_SIZE + 1];
            serialize_header(&header, buf);
            buf[HEADER_SENSOR_SIZE] = val;
            log_msg(TAG, "sw val: %d", val);
        #if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
        #endif
        #if CONFIG_USE_ESPNOW
        #endif
        }
    }
    vTaskDelete(NULL);
}


static void ky023_task_xy(void * params) {
    esp_err_t err;

    uint8_t result[READ_LEN] = {0};
    uint32_t ret_num = 0;
    
    int raw_val_x;
    int raw_val_y;

    while (monitoring)
    {
        err = adc_continuous_read(adc_handle, result, READ_LEN, &ret_num, pdMS_TO_TICKS(10));
        
        if (err == ESP_OK) {
            // Le DMA nous donne un paquet de mesures, on l'analyse
            for (int i = 0; i < ret_num; i += SOC_ADC_DIGI_RESULT_BYTES) {
                adc_digi_output_data_t *p = (adc_digi_output_data_t *)&result[i];
                
                // On regarde de quel canal vient la mesure actuelle
                if (p->type1.channel == KY023_ADC_CHANNEL_X) {
                    raw_val_x = p->type1.data;
                } else if (p->type1.channel == KY023_ADC_CHANNEL_Y) {
                    raw_val_y = p->type1.data;
                }
            }

            header_sensor_t header = {0};
            header.esp_id = (uint8_t)CONFIG_ESP_ID;
            header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            header.type = SENSOR_TYPE_KY023_XY;
            uint8_t buf[HEADER_SENSOR_SIZE + 8];
            serialize_header(&header, buf);
            memcpy(&buf[HEADER_SENSOR_SIZE], &raw_val_x, 4);
            memcpy(&buf[HEADER_SENSOR_SIZE + 4], &raw_val_y, 4);

            //log_msg(TAG, "x: %d, y: %d", raw_val_x, raw_val_y);

        #if CONFIG_USE_UDPLIB
            send_udp_sensor(buf, sizeof(buf));
        #endif
        #if CONFIG_USE_ESPNOW
        #endif
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelete(NULL);
}

#endif


#if CONFIG_USE_ESP

#if CONFIG_USE_WIFI
#include "wifiLib.h"
#endif
#include "esp_system.h"

#if CONFIG_USE_UDPLIB
#include "udpLib.h"
#endif

#include "driver/temperature_sensor.h"
#include "esp_heap_caps.h"

#if CONFIG_FREE_RTOS_TASK_DEBUG

#define MAX_TRACKED_TASKS 32

// Structure pour mémoriser l'état d'une tâche au tour précédent
typedef struct {
    UBaseType_t task_number;
    uint32_t last_runtime;
    bool active;
    bool visited; // Pour nettoyer les tâches qui ont été supprimées
} TaskHistory_t;

static void get_detailed_cpu_usage(void) {
    static TaskHistory_t history[MAX_TRACKED_TASKS] = {0};
    
    UBaseType_t uxArraySize = uxTaskGetNumberOfTasks();
    TaskStatus_t *pxTaskStatusArray = malloc(uxArraySize * sizeof(TaskStatus_t));
    
    if (pxTaskStatusArray == NULL) return;

    uint32_t ulTotalRunTime;
    uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalRunTime);
    
    // 1. Calculer le delta du temps global du système
    static uint32_t ulLastTotalRunTime = 0;
    uint32_t ulDeltaTotalTime = ulTotalRunTime - ulLastTotalRunTime;
    ulLastTotalRunTime = ulTotalRunTime;

    // Prépare l'historique pour le marquage
    for (int i = 0; i < MAX_TRACKED_TASKS; i++) {
        history[i].visited = false;
    }

    if (ulDeltaTotalTime > 0) {
        // 2. Parcourir toutes les tâches actives actuelles
        for (UBaseType_t x = 0; x < uxArraySize; x++) {
            UBaseType_t task_num = pxTaskStatusArray[x].xTaskNumber;
            uint32_t current_runtime = pxTaskStatusArray[x].ulRunTimeCounter;
            uint32_t delta_task_time = 0;
            int history_idx = -1;

            // Chercher la tâche dans notre historique
            for (int i = 0; i < MAX_TRACKED_TASKS; i++) {
                if (history[i].active && history[i].task_number == task_num) {
                    history_idx = i;
                    break;
                }
            }

            if (history_idx != -1) {
                // Tâche déjà connue : on calcule le delta
                delta_task_time = current_runtime - history[history_idx].last_runtime;
                history[history_idx].last_runtime = current_runtime;
                history[history_idx].visited = true;
            } else {
                // Nouvelle tâche : on lui trouve une place libre dans l'historique
                for (int i = 0; i < MAX_TRACKED_TASKS; i++) {
                    if (!history[i].active) {
                        history[i].task_number = task_num;
                        history[i].last_runtime = current_runtime;
                        history[i].active = true;
                        history[i].visited = true;
                        break;
                    }
                }
                // Premier tour pour cette tâche, pas encore de delta valide
                delta_task_time = 0; 
            }

            // 3. Calculer le POURCENTAGE CPU réel de la tâche
            // Sur ESP32 (Dual Core), ulDeltaTotalTime correspond au temps d'un cœur.
            // Une tâche ne pouvant tourner que sur un seul cœur à la fois, son max est de 100%.
            float task_cpu_percent = ((float)delta_task_time / (float)ulDeltaTotalTime) * 100.0f;
            if (task_cpu_percent > 100.0f) task_cpu_percent = 100.0f; // Sécurité anti-jitter

            // --- TU AS TES DONNÉES ICI ---
            const char* name = pxTaskStatusArray[x].pcTaskName;
            uint32_t stack = pxTaskStatusArray[x].usStackHighWaterMark;
            
            // Log de débug direct sur le port série (pas de ESP_LOG pour éviter la récursion !)
            log_msg(TAG, "Task: %-12s | CPU: %5.1f%% | Min Stack: %lu bytes", 
                   name, task_cpu_percent, (unsigned long)stack);
        }
    }

    // 4. Nettoyage : si une tâche a disparu de FreeRTOS, on libère son slot dans l'historique
    for (int i = 0; i < MAX_TRACKED_TASKS; i++) {
        if (history[i].active && !history[i].visited) {
            history[i].active = false;
        }
    }

    free(pxTaskStatusArray);
}
#endif

static void get_global_cpu_usage(float *out_core0, float *out_core1) {
    UBaseType_t uxArraySize = uxTaskGetNumberOfTasks();
    TaskStatus_t *pxTaskStatusArray = malloc(uxArraySize * sizeof(TaskStatus_t));
    
    if (pxTaskStatusArray != NULL) {
        uint32_t ulTotalRunTime;
        uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalRunTime);
        
        static uint32_t last_total = 0, last_idle0 = 0, last_idle1 = 0;
        uint32_t current_idle0 = 0, current_idle1 = 0;

        // Trouver les tâches IDLE
        for (UBaseType_t x = 0; x < uxArraySize; x++) {
            if (strcmp(pxTaskStatusArray[x].pcTaskName, "IDLE0") == 0) {
                current_idle0 = pxTaskStatusArray[x].ulRunTimeCounter;
            } else if (strcmp(pxTaskStatusArray[x].pcTaskName, "IDLE1") == 0) {
                current_idle1 = pxTaskStatusArray[x].ulRunTimeCounter;
            }
        }

        uint32_t delta_total = ulTotalRunTime - last_total;
        if (delta_total > 0) {
            uint32_t delta_idle0 = current_idle0 - last_idle0;
            uint32_t delta_idle1 = current_idle1 - last_idle1;

            // Calcul du pourcentage d'utilisation
            *out_core0 = 100.0f * (1.0f - ((float)delta_idle0 / (float)delta_total));
            *out_core1 = 100.0f * (1.0f - ((float)delta_idle1 / (float)delta_total));
        }

        last_total = ulTotalRunTime;
        last_idle0 = current_idle0;
        last_idle1 = current_idle1;

        free(pxTaskStatusArray);
    }
}

#define ESP_PACKET_SIZE (5 * sizeof(uint32_t) + 2 * sizeof(int8_t) + 3 * sizeof(float) + 3 * sizeof(uint8_t))

typedef struct esp_packet_st {
    float esp_deg;
    int8_t rssi;
    uint8_t reset_reason;
    uint32_t free_dram;
    uint32_t free_dram_block;
    uint32_t free_psram;
    uint32_t free_psram_block;
    uint8_t angle;
    int8_t motor;
    uint32_t nb_packets;
    float core0;
    float core1;
    uint8_t drive_mode;
} esp_packet_t;

static void serialize_esp_packet(esp_packet_t *pkt, uint8_t *buf) {
    uint16_t len = HEADER_SENSOR_SIZE; 
    memcpy(&buf[len], &pkt->esp_deg, sizeof(float));
    len += sizeof(float);
    memcpy(&buf[len], &pkt->rssi, sizeof(int8_t));
    len += sizeof(int8_t);
    memcpy(&buf[len], &pkt->reset_reason, sizeof(uint8_t));
    len += sizeof(uint8_t);
    memcpy(&buf[len], &pkt->free_dram, sizeof(uint32_t));
    len += sizeof(uint32_t);
    memcpy(&buf[len], &pkt->free_dram_block, sizeof(uint32_t));
    len += sizeof(uint32_t);
    memcpy(&buf[len], &pkt->free_psram, sizeof(uint32_t));
    len += sizeof(uint32_t);
    memcpy(&buf[len], &pkt->free_psram_block, sizeof(uint32_t));
    len += sizeof(uint32_t);
    memcpy(&buf[len], &pkt->angle, sizeof(uint8_t));
    len += sizeof(uint8_t);
    memcpy(&buf[len], &pkt->motor, sizeof(int8_t));
    len += sizeof(int8_t);
    memcpy(&buf[len], &pkt->nb_packets, sizeof(uint32_t));
    len += sizeof(uint32_t);
    memcpy(&buf[len], &pkt->core0, sizeof(float));
    len += sizeof(float);
    memcpy(&buf[len], &pkt->core1, sizeof(float));
    len += sizeof(float);
    memcpy(&buf[len], &pkt->drive_mode, sizeof(uint8_t));
}

static void print_esp(esp_packet_t *pkt) {
    log_msg(TAG, "ESP Temp: %.3f°C", pkt->esp_deg);
    log_msg(TAG, "STA RSSI: %d dBm", pkt->rssi);
    log_msg(TAG, "Last reset reason: %u", pkt->reset_reason);
    
    // DRAM : Affichage brut + conversion en Ko
    log_msg(TAG, "Free DRAM: %lu bytes (%.2f KB)", (unsigned long)pkt->free_dram, pkt->free_dram / 1024.0f);
    log_msg(TAG, "Max DRAM block: %lu bytes (%.2f KB)", (unsigned long)pkt->free_dram_block, pkt->free_dram_block / 1024.0f);
    
    // PSRAM : Affichage brut + conversion en Mo (vu qu'on a 8 Mo sur le S3)
    log_msg(TAG, "Free PSRAM: %lu bytes (%.2f MB)", (unsigned long)pkt->free_psram, pkt->free_psram / (1024.0f * 1024.0f));
    log_msg(TAG, "Max PSRAM block: %lu bytes (%.2f MB)", (unsigned long)pkt->free_psram_block, pkt->free_psram_block / (1024.0f * 1024.0f));
    
    log_msg(TAG, "Angle: %u°", pkt->angle);
    log_msg(TAG, "Motor: %d", pkt->motor);
    log_msg(TAG, "Drive mode: %d", pkt->drive_mode);
    log_msg(TAG, "Packets received: %lu", (unsigned long)pkt->nb_packets);
    log_msg(TAG, "CPU Usage: Core0: %.3f%%, Core1: %.3f%%", pkt->core0, pkt->core1);
}

static void esp_monitor_task(void *params) {
    esp_err_t err;

    float tsens_out;
    temperature_sensor_config_t temp_sensor = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    temperature_sensor_handle_t temp_handle = NULL;
    err = temperature_sensor_install(&temp_sensor, &temp_handle);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) installing ESP temperature sensor", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    err = temperature_sensor_enable(temp_handle);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) enabling ESP temperature sensor", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    while (monitoring) {

        esp_packet_t esp_pck = {0};
        
        err = temperature_sensor_get_celsius(temp_handle, &tsens_out);
        if (err != ESP_OK) {
            log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) reading ESP temperature sensor", esp_err_to_name(err));
        } else {
            esp_pck.esp_deg = tsens_out;
        }

        #if CONFIG_USE_WIFI
        int rssi = 0;
        err = sta_get_rssi(&rssi);
        if (err != ESP_OK) {
            log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) reading STA RSSI", esp_err_to_name(err));
        } else {
            esp_pck.rssi = (int8_t)rssi;
        }
        #endif

        //DRAM + PSRAM on ESP32S3, we can seperate
        //log_msg(TAG, "min free heap size: %u", esp_get_minimum_free_heap_size());
        //log_msg(TAG, "free heap size: %u", esp_get_free_heap_size());

        esp_pck.reset_reason = (uint8_t)esp_reset_reason();

        
        esp_pck.free_dram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        esp_pck.free_dram_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

        esp_pck.free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        esp_pck.free_psram_block = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

        //uint32_t free_iram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_IRAM_8BIT);
        //log_msg(TAG, "free iram: %u", free_iram);

        #if CONFIG_USE_LEDLIB
        uint8_t angle = 0;
        int16_t motor = 0;
        err = get_servo_angle(&angle);
        if (err != ESP_OK) {
            log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) reading ANGLE", esp_err_to_name(err));
        } else {
            esp_pck.angle = angle;
        }
        err = get_motor_percent(&motor);
        if (err != ESP_OK) {
            log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) reading MOTOR", esp_err_to_name(err));
        } else {
            esp_pck.motor = motor;
        }
        esp_pck.drive_mode = (uint8_t)get_drive_mode();
        #endif

        #if CONFIG_USE_UDPLIB
        esp_pck.nb_packets = get_command_packet_received();
        #endif

        float core0 = 0.0, core1 = 0.0;
        get_global_cpu_usage(&core0, &core1);
        esp_pck.core0 = core0;
        esp_pck.core1 = core1;

        //print_esp(&esp_pck);

        #if CONFIG_FREE_RTOS_TASK_DEBUG
        get_detailed_cpu_usage();
        #endif

        header_sensor_t header = {0};
        header.esp_id = (uint8_t)CONFIG_ESP_ID;
        header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
        header.type = SENSOR_TYPE_ESP;
        uint8_t buf[HEADER_SENSOR_SIZE + ESP_PACKET_SIZE];
        serialize_header(&header, buf);
        serialize_esp_packet(&esp_pck, buf);
        
    #if CONFIG_USE_UDPLIB
        send_udp_sensor(buf, sizeof(buf));
    #endif
        
        vTaskDelay(pdMS_TO_TICKS(500));
        
    }
    vTaskDelete(NULL);
}

#endif


esp_err_t init_sensors() {

    esp_err_t err;
    BaseType_t res;

    if (monitoring) {
        log_msg_lvl(ESP_LOG_WARN, TAG, "Sensors already initialized");
        return ESP_FAIL;
    }
    monitoring = true;

#if CONFIG_USE_HCSR04
    
    err = init_hcsr();
    if (err == ESP_OK){
        for (int i = 0; i < sizeof(hc_cfgs) / sizeof(hc_cfg_t); i++) {
            char name[20];
            snprintf(name, sizeof(name), "hc_monitoring_%d", hc_cfgs[i].hc_id);
            res = xTaskCreate(hc_task, name, 4096, &hc_cfgs[i], 5, NULL);
            if (res != pdPASS) {
                log_msg_lvl(ESP_LOG_ERROR, TAG, 
                    "Error (%d) creating HC_%d monitoring task", hc_cfgs[i].hc_id, res);
                return ESP_FAIL;
            }
            vTaskDelay(pdMS_TO_TICKS(25));
        }
    }

#endif

#if CONFIG_USE_INA226
    ina_info_t ina = {0}, ina_temp;
    res = xTaskCreate(ina_task, "ina_monitoring", 2048, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating INA monitoring task", res);
        return ESP_FAIL;
    }
#endif

#if CONFIG_USE_MPU9250
    mpu_info_t mpu = {0}, mpu_temp;
    res = xTaskCreate(mpu_task, "mpu_monitoring", 2048, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating mpu monitoring task", res);
        return ESP_FAIL;
    }
#endif

#if CONFIG_USE_KY035
    err = init_ky();
    if (err != ESP_OK){
        return ESP_FAIL;
    }
    ky_info_t ky = {0}, ky_temp;
    res = xTaskCreate(ky_task, "ky_monitoring", 2048, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating KY monitoring task", res);
        return ESP_FAIL;
    }
#endif

#if CONFIG_USE_RFID_RC522

    res = xTaskCreate(rfid_rc522_task, "rfid_rc522_monitoring", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating rfid_rc522 monitoring task", res);
        return ESP_FAIL;
    }

#endif

#if CONFIG_USE_RCWL_0515

    res = xTaskCreate(rcwl_task, "rcwl_0515_monitoring", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating rcwl_0515 monitoring task", res);
        return ESP_FAIL;
    }

#endif

#if CONFIG_USE_VL53L1X

    res = xTaskCreate(vl_task, "vl53l1x_monitoring", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating vl53l1x monitoring task", res);
        return ESP_FAIL;
    }

#endif

#if CONFIG_USE_AS5600

    res = xTaskCreate(as_task, "as5600_monitoring", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating as5600 monitoring task", res);
        return ESP_FAIL;
    }

#endif

#if CONFIG_USE_KY003

    res = xTaskCreate(ky_003_task, "ky_003_monitoring", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating KY-003 monitoring task", res);
        return ESP_FAIL;
    }

#endif

#if CONFIG_USE_FC33

    res = xTaskCreate(fc_33_task, "fc_33_monitoring", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating FC-33 monitoring task", res);
        return ESP_FAIL;
    }

#endif

#if CONFIG_USE_KY033

    res = xTaskCreate(ky033_task, "ky_033_monitoring", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating KY-033 monitoring task", res);
        return ESP_FAIL;
    }

#endif

#if CONFIG_USE_KY002

    res = xTaskCreate(ky002_task, "ky_002_monitoring", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating KY-002 monitoring task", res);
        return ESP_FAIL;
    }

#endif

#if CONFIG_USE_KY040

    res = xTaskCreate(ky040_task, "ky_040_monitoring", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating KY-040 monitoring task", res);
        return ESP_FAIL;
    }

#endif

#if CONFIG_USE_DHT11

    res = xTaskCreate(dht11_task, "dht11_monitoring", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating DHT11 monitoring task", res);
        return ESP_FAIL;
    }

#endif

#if CONFIG_USE_KY020

    res = xTaskCreate(ky020_task, "ky020_monitoring", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating KY-020 monitoring task", res);
        return ESP_FAIL;
    }

#endif

#if CONFIG_USE_KY018

    res = xTaskCreate(ky018_task, "ky018_monitoring", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating KY-018 monitoring task", res);
        return ESP_FAIL;
    }

#endif

#if CONFIG_USE_KY031

    res = xTaskCreate(ky031_task, "ky031_monitoring", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating KY-031 monitoring task", res);
        return ESP_FAIL;
    }

#endif

#if CONFIG_USE_KY017

    res = xTaskCreate(ky017_task, "ky017_monitoring", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating KY-017 monitoring task", res);
        return ESP_FAIL;
    }

#endif

#if CONFIG_USE_KY005

    res = xTaskCreate(ky005_task, "ky005_monitoring", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating KY-005 monitoring task", res);
        return ESP_FAIL;
    }

#endif

#if CONFIG_USE_KY022

    res = xTaskCreate(ky022_task, "ky022_monitoring", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating KY-022 monitoring task", res);
        return ESP_FAIL;
    }

#endif

#if CONFIG_USE_KY021

    res = xTaskCreate(ky021_task, "ky_021_monitoring", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating KY-021 monitoring task", res);
        return ESP_FAIL;
    }

#endif

#if CONFIG_USE_KY004

    res = xTaskCreate(ky004_task, "ky_004_monitoring", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating KY-004 monitoring task", res);
        return ESP_FAIL;
    }

#endif

#if CONFIG_USE_KY039

    res = xTaskCreate(ky039_task, "ky_039_monitoring", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating KY-039 monitoring task", res);
        return ESP_FAIL;
    }

#endif

#if CONFIG_USE_KY032

    res = xTaskCreate(ky032_task, "ky_032_monitoring", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating KY-032 monitoring task", res);
        return ESP_FAIL;
    }

#endif

#if CONFIG_USE_KY023

    err = init_ky023();
    if (err != ESP_OK){
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) during ky023 init", esp_err_to_name(err));
    } else {
        res = xTaskCreate(ky023_task_sw, "ky_023_sw_monitoring", 4096, NULL, 5, NULL);
        if (res != pdPASS) {
            log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating KY-023 monitoring task sw", res);
            return ESP_FAIL;
        }
        res = xTaskCreate(ky023_task_xy, "ky_023_xy_monitoring", 4096, NULL, 5, NULL);
        if (res != pdPASS) {
            log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating KY-023 monitoring task xy", res);
            return ESP_FAIL;
        }
    }


#endif

#if CONFIG_USE_ESP
    res = xTaskCreate(esp_monitor_task, "esp_monitoring", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating ESP monitoring task", res);
        return ESP_FAIL;
    }
#endif
    
    return ESP_OK;
}