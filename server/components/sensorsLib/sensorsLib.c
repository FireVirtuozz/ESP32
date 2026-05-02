#include "sensorsLib.h"
#include "driver/gpio.h"
#include "logLib.h"

#include "esp_rom_sys.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/i2c_master.h"

#include "udpLib.h"

#define MONITOR_PERIOD 500

static const char * TAG = "sensors_library";

static volatile bool monitoring = false;

#if USE_HCSR04

#define TRIG_PIN 27
#define ECHO_PIN 26
#define PULSE_TRIG_DURATION 10 //10us
#define ECHO_TIMEOUT 60 //60ms
#define HC_PERIOD 50

static SemaphoreHandle_t sem_hcsr = NULL;
static volatile int64_t echo_duration;

static QueueHandle_t hc_queue = NULL;

static void IRAM_ATTR echo_isr_handler(void* arg)
{
    BaseType_t taskAwoken = pdFALSE;
    static int64_t last_timestamp = -1;

    if (gpio_get_level(ECHO_PIN) == 1) {
        last_timestamp = esp_timer_get_time();
    } else { //falling edge
        if (last_timestamp > 0) {
            echo_duration = esp_timer_get_time() - last_timestamp;
            xSemaphoreGiveFromISR(sem_hcsr, &taskAwoken);
            portYIELD_FROM_ISR(taskAwoken); //wake up the task waiting for semaphore (trig_echo)
        }
        last_timestamp = -1;  // reset
    }
}

static void print_hc(int64_t val) {
    log_msg(TAG, "value received: %lld, to centimeters: %.1fcm",
            val, val / 58.0);
}

esp_err_t init_hcsr() {
    esp_err_t err;

    //avoid re-initialization
    if (sem_hcsr != NULL) {
        log_msg(TAG, "HC-SR04 already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    sem_hcsr = xSemaphoreCreateBinary();
    if (sem_hcsr == NULL) {
        log_msg(TAG, "Error creating semaphore");
        return ESP_ERR_NOT_ALLOWED;
    }

    hc_queue = xQueueCreate((MONITOR_PERIOD / HC_PERIOD) + 2, sizeof(int64_t));
    if (hc_queue == NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error creating HC queue");
        return ESP_ERR_NOT_ALLOWED;
    }

    err = gpio_reset_pin(TRIG_PIN);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), TRIG_PIN);
        return err;
    }
    err = gpio_reset_pin(ECHO_PIN);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), ECHO_PIN);
        return err;
    }

    err = gpio_set_direction(TRIG_PIN, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction pin %d", esp_err_to_name(err), TRIG_PIN);
        return err;
    }
    err = gpio_set_direction(ECHO_PIN, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction pin %d", esp_err_to_name(err), ECHO_PIN);
        return err;
    }

    err = gpio_set_intr_type(ECHO_PIN, GPIO_INTR_ANYEDGE);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting interrupt type pin %d", esp_err_to_name(err), ECHO_PIN);
        return err;
    }
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        log_msg(TAG, "Error (%s) installing isr pin %d", esp_err_to_name(err), ECHO_PIN);
        return err;
    }
    err = gpio_isr_handler_add(ECHO_PIN, echo_isr_handler, NULL);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding isr handler pin %d", esp_err_to_name(err), ECHO_PIN);
        return err;
    }
    //or gpio_isr_register()
    log_msg(TAG, "HC-SR04 initialized");
    return ESP_OK;
    
}

int64_t trigger_echo() {
    esp_err_t err;

    if (sem_hcsr == NULL || hc_queue == NULL) {
        return -1;
    }

    //log_msg(TAG, "Triggering echo..");

    err = gpio_set_level(TRIG_PIN, 1);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting level on pin : %d", esp_err_to_name(err), TRIG_PIN);
        return -1;
    }
    esp_rom_delay_us(PULSE_TRIG_DURATION);
    err = gpio_set_level(TRIG_PIN, 0);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting level on pin : %d", esp_err_to_name(err), TRIG_PIN);
        return -1;
    }

    //log_msg(TAG, "Echo triggered");

    if (xSemaphoreTake(sem_hcsr, pdMS_TO_TICKS(ECHO_TIMEOUT)) == pdFALSE) {
        return -1; //after tiemout
    } else {
        return echo_duration; //semaphore given from ISR
    }
}

/**
 * 50 ms monitoring HC task
 */
static void hc_task(void * params) {
    esp_err_t err;
    
    int64_t val_hc;
    err = init_hcsr();
    if (err != ESP_OK){
        return;
    }

    while (monitoring)
    {
        val_hc = trigger_echo(); //blocking

        if (val_hc >= 0 && val_hc < 30000) {
            xQueueSend(hc_queue, &val_hc, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(HC_PERIOD));
    }
    return;
}

#endif

#if USE_INA226 || USE_MPU9250

#define SCL_GPIO 33
#define SDA_GPIO 32

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

#if USE_INA226

#define INA_ADDR 0x40
#define INA_PERIOD 70

static QueueHandle_t ina_queue = NULL;

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

    if (ina_queue != NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "INA already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    ina_queue = xQueueCreate((MONITOR_PERIOD / INA_PERIOD) + 2, sizeof(ina_info_t));
    if (ina_queue == NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error creating INA queue");
        return ESP_ERR_NOT_ALLOWED;
    }

    //config of i2c registers, gpio, start
    if (master_handle == NULL) {
        err = i2c_new_master_bus(&master_cfg, &master_handle);
        if (err != ESP_OK) {
            log_msg(TAG, "Error (%s) allocating I²C master bus", esp_err_to_name(err));
            return err;
        }
    }

    err = i2c_master_bus_add_device(master_handle, &ina_cfg, &ina_handle);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding device to master bus", esp_err_to_name(err));
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
        log_msg(TAG, "Error (%s) transmitting config frame", esp_err_to_name(err));
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
        log_msg(TAG, "Error (%s) transmitting calibration frame", esp_err_to_name(err));
        return err;
    }

    log_msg(TAG, "INA226 initialized");

    return ESP_OK;
    
}

esp_err_t get_ina_info(ina_info_t *ina_info) {
    if (ina_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ina_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
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
        log_msg(TAG, "Error (%s) receiving shunt", esp_err_to_name(err));
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

/**
 * Periodic INA monitoring task
 */
static void ina_task(void * params) {
    esp_err_t err;
    ina_info_t ina_info;

    err = init_ina();
    if (err != ESP_OK){
        return;
    }

    while (monitoring)
    {
        err = get_ina_info(&ina_info);

        if (err == ESP_OK) {
            xQueueSend(ina_queue, &ina_info, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(INA_PERIOD));
    }
    return;
}

#endif

#if USE_KY003

#define KY_GPIO 34
#define KY_PERIOD 10

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// Variables globales pour le driver
static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t adc1_cali_handle = NULL;

// Choisis ton canal (ADC1_CHANNEL_6 est sur le GPIO 34)
#define HALL_ADC_CHANNEL ADC1_CHANNEL_6

static volatile ky_info_t ky_info = {0};
static QueueHandle_t ky_queue = NULL;

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

    if (ky_queue != NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "KY already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    ky_queue = xQueueCreate((MONITOR_PERIOD / KY_PERIOD) + 2, sizeof(ky_info_t));
    if (ky_queue == NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error creating KY queue");
        return ESP_ERR_NOT_ALLOWED;
    }

    /*
    err = gpio_reset_pin(KY_GPIO);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting pin %d", esp_err_to_name(err), KY_GPIO);
        return err;
    }

    err = gpio_set_direction(KY_GPIO, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting direction pin %d", esp_err_to_name(err), KY_GPIO);
        return err;
    }
        */

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

    // 3. Calibration (line fitting, ESP32 classique)
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_line_fitting(&cali_config, &adc1_cali_handle);
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
    if (ky_queue == NULL) {
        return NULL;
    }

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
    /*
    esp_err_t err;
    ky_info_t *ky_info;

    err = init_ky();
    if (err != ESP_OK){
        return;
    }

    while (monitoring)
    {
        ky_info = get_signal_info();

        if (ky_info != NULL) {
            xQueueSend(ky_queue, ky_info, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(KY_PERIOD));
    }
    return;
    */

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
                xQueueSend(ky_queue, &snapshot, 0);
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
//todo if there is too much errors

#endif

#if USE_MPU9250

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

static QueueHandle_t mpu_queue = NULL;

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

    if (mpu_queue != NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "MPU already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    mpu_queue = xQueueCreate((MONITOR_PERIOD / MPU_PERIOD) + 2, sizeof(mpu_info_t));
    if (mpu_queue == NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error creating MPU queue");
        return ESP_ERR_NOT_ALLOWED;
    }

    //config of i2c registers, gpio, start
    if (master_handle == NULL) {
        err = i2c_new_master_bus(&master_cfg, &master_handle);
        if (err != ESP_OK) {
            log_msg(TAG, "Error (%s) allocating I²C master bus", esp_err_to_name(err));
            return err;
        }
    }

    err = i2c_master_bus_add_device(master_handle, &mpu_cfg, &mpu_handle);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding mpu device to master bus", esp_err_to_name(err));
        return err;
    }

    err = i2c_master_bus_add_device(master_handle, &bmp_cfg, &bmp_handle);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) adding bmp device to master bus", esp_err_to_name(err));
        return err;
    }

    //reboot mpu
    uint8_t pwr_data[] = {0x6B, 0x00};

    pwr_data[1] |= 1 << 7; //reset

    err = i2c_master_transmit(mpu_handle, pwr_data, sizeof(pwr_data), -1);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) waking up mpu", esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(100)); //wait for reboot

    //mpu device identifier
    uint8_t reg = 0x75;
    uint8_t who;
    err = i2c_master_transmit_receive(mpu_handle, &reg, 1, &who, 1, -1);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) getting who am i mpu", esp_err_to_name(err));
        return err;
    }
    log_msg(TAG, "WHO_AM_I mpu: 0x%02X", who);

    //reboot bmp
    uint8_t reset_bmp_data[] = {0xE0, 0xB6};

    err = i2c_master_transmit(bmp_handle, reset_bmp_data, sizeof(reset_bmp_data), -1);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) resetting bmp", esp_err_to_name(err));
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
        log_msg(TAG, "Error (%s) waking up bmp", esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(100)); //wait for config setup

    //device identifier
    uint8_t reg_bmp = 0xD0;
    uint8_t who_bmp;
    err = i2c_master_transmit_receive(bmp_handle, &reg_bmp, 1, &who_bmp, 1, -1);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) getting who am i bmp", esp_err_to_name(err));
        return err;
    }
    log_msg(TAG, "WHO_AM_I bmp: 0x%02X", who_bmp);

    //calibration temperature
    uint8_t reg_dig_t = 0x88;
    uint8_t buf_dig_t[6];
    err = i2c_master_transmit_receive(bmp_handle, &reg_dig_t, 1, buf_dig_t, sizeof(buf_dig_t), -1);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) receiving mpu info", esp_err_to_name(err));
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
        log_msg(TAG, "Error (%s) reading pressure calib", esp_err_to_name(err));
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
    if (mpu_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err;

    uint8_t reg = 0x3B;
    uint8_t buf[14];  // 14 bytes : accel(6) + temp(2) + gyro(6)
    err = i2c_master_transmit_receive(mpu_handle, &reg, 1, buf, sizeof(buf), -1);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) receiving mpu info", esp_err_to_name(err));
        return err;
    }

    uint8_t reg_bmp = 0xF7;
    uint8_t buf_bmp[6];  // 6 bytes : temp(3) + pressure(3)
    err = i2c_master_transmit_receive(bmp_handle, &reg_bmp, 1, buf_bmp, sizeof(buf_bmp), -1);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) receiving mpu info", esp_err_to_name(err));
        return err;
    }

    mpu_info->accel_x = ((uint16_t)buf[0]  << 8) | buf[1];
    mpu_info->accel_y = ((uint16_t)buf[2]  << 8) | buf[3];
    mpu_info->accel_z = ((uint16_t)buf[4]  << 8) | buf[5];
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

/**
 * 50 ms monitoring MPU task
 */
static void mpu_task(void * params) {
    esp_err_t err;
    mpu_info_t mpu_info;

    err = init_mpu();
    if (err != ESP_OK){
        return;
    }

    while (monitoring)
    {
        err = get_mpu_info(&mpu_info);

        if (err == ESP_OK) {
            xQueueSend(mpu_queue, &mpu_info, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(MPU_PERIOD));
    }
    return;
}

#endif

static void monitoring_task(void* params) {

    uint16_t count = 0;
    BaseType_t res;

#if USE_HCSR04
    int64_t echo_temp = 0, echo_res = 0;
    res = xTaskCreate(hc_task, "hc_monitoring", 2048, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg(TAG, "Error (%d) creating HC monitoring task", res);
        return;
    }
#endif

#if USE_INA226
    ina_info_t ina = {0}, ina_temp;
    res = xTaskCreate(ina_task, "ina_monitoring", 2048, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg(TAG, "Error (%d) creating INA monitoring task", res);
        return;
    }
#endif

#if USE_MPU9250
    mpu_info_t mpu = {0}, mpu_temp;
    res = xTaskCreate(mpu_task, "mpu_monitoring", 2048, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg(TAG, "Error (%d) creating mpu monitoring task", res);
        return;
    }
#endif

#if USE_KY003
esp_err_t err;
   err = init_ky();
    if (err != ESP_OK){
        return;
    }
    ky_info_t ky = {0}, ky_temp;
    res = xTaskCreate(ky_task, "ky_monitoring", 2048, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg(TAG, "Error (%d) creating KY monitoring task", res);
        return;
    }

    
#endif

    uint8_t frame_size = sizeof(header_udp_frame_t);

#if USE_KY003
    frame_size += sizeof(ky_info_t);
#endif
#if USE_HCSR04
    frame_size += sizeof(int64_t);
#endif
#if USE_MPU9250
    frame_size += sizeof(mpu_info_t);
#endif
#if USE_INA226
    frame_size += sizeof(ina_info_t);
#endif
    uint8_t frame_buf[frame_size];

    header_udp_frame_t frame = {0};
#if USE_KY003
    frame.flags |= 1 << 0;
#endif
#if USE_HCSR04
    frame.flags |= 1 << 1;
#endif
#if USE_MPU9250
    frame.flags |= 1 << 2;
#endif
#if USE_INA226
    frame.flags |= 1 << 3;
#endif

    frame.type = 0; //monitor frame type
    frame.timestamp = (uint32_t)(esp_timer_get_time() / 1000);

    while (monitoring)
    {
        uint8_t offset_frame = 0;
        frame_buf[offset_frame] = frame.type;
        offset_frame++;
        frame_buf[offset_frame] = frame.flags;
        offset_frame++;
        memcpy(&frame_buf[offset_frame], &frame.timestamp, sizeof(uint32_t));
        offset_frame += sizeof(uint32_t);
        
    #if USE_KY003
        int signals_in_this_window = 0;
        if (ky_queue != NULL) {
            int64_t total_duration = 0;

            while (xQueueReceive(ky_queue, &ky_temp, 0) == pdTRUE) {
                signals_in_this_window++;
                total_duration += ky_temp.signal_duration;
            }

            if (signals_in_this_window > 0) {
                // VRAIE MOYENNE : Somme des temps / nombre d'impulsions
                ky.signal_duration = total_duration / signals_in_this_window;
                ky.signal_count += signals_in_this_window; // Cumul total
            } else {
                // Optionnel : si on n'a rien reçu, la vitesse est peut-être 0
                ky.signal_duration = 0; 
            }
            //log_msg(TAG, "count window: %d", signals_in_this_window);
        }
        print_ky(&ky);
        memcpy(&frame_buf[offset_frame], &ky.signal_count, sizeof(ky.signal_count));
        offset_frame += sizeof(ky.signal_count);
        memcpy(&frame_buf[offset_frame], &ky.signal_duration, sizeof(ky.signal_duration));
        offset_frame += sizeof(ky.signal_duration);
    #endif

    #if USE_HCSR04
        if (hc_queue != NULL && uxQueueMessagesWaiting(hc_queue) > 0) {
            echo_res = 0;
            count = 0;
            while (xQueueReceive(hc_queue, &echo_temp, 0) == pdTRUE) {
                echo_res += echo_temp;
                count++;
            }
            if (count > 0) {
                echo_res /= count;
            }
        }
        print_hc(echo_res);
        memcpy(&frame_buf[offset_frame], &echo_res, sizeof(int64_t));
        offset_frame += sizeof(int64_t);

    #endif
    
    #if USE_MPU9250
        if (mpu_queue != NULL && uxQueueMessagesWaiting(mpu_queue) > 0) {
            int32_t ax=0,ay=0,az=0,gx=0,gy=0,gz=0,tm=0,tb=0,p=0;
            count = 0;
            while (xQueueReceive(mpu_queue, &mpu_temp, 0) == pdTRUE) {
                ax += mpu_temp.accel_x;
                ay += mpu_temp.accel_y;
                az += mpu_temp.accel_z;
                gx += mpu_temp.gyro_x;
                gy += mpu_temp.gyro_y;
                gz += mpu_temp.gyro_z;
                tm += mpu_temp.temp_mpu;
                tb += mpu_temp.temp_bmp;
                p  += mpu_temp.pressure;
                count++;
            }
            if (count > 0) {
                mpu.accel_x  = ax / count;
                mpu.accel_y  = ay / count;
                mpu.accel_z  = az / count;
                mpu.gyro_x   = gx / count;
                mpu.gyro_y   = gy / count;
                mpu.gyro_z   = gz / count;
                mpu.temp_mpu = tm / count;
                mpu.temp_bmp = tb / count;
                mpu.pressure = p  / count;
            }
        }
        print_mpu(&mpu);
        memcpy(&frame_buf[offset_frame], &mpu.accel_x, sizeof(mpu.accel_x));
        offset_frame += sizeof(mpu.accel_x);
        memcpy(&frame_buf[offset_frame], &mpu.accel_y, sizeof(mpu.accel_y));
        offset_frame += sizeof(mpu.accel_y);
        memcpy(&frame_buf[offset_frame], &mpu.accel_z, sizeof(mpu.accel_z));
        offset_frame += sizeof(mpu.accel_z);
        memcpy(&frame_buf[offset_frame], &mpu.gyro_x, sizeof(mpu.gyro_x));
        offset_frame += sizeof(mpu.gyro_x);
        memcpy(&frame_buf[offset_frame], &mpu.gyro_y, sizeof(mpu.gyro_y));
        offset_frame += sizeof(mpu.gyro_y);
        memcpy(&frame_buf[offset_frame], &mpu.gyro_z, sizeof(mpu.gyro_z));
        offset_frame += sizeof(mpu.gyro_z);
        memcpy(&frame_buf[offset_frame], &mpu.temp_mpu, sizeof(mpu.temp_mpu));
        offset_frame += sizeof(mpu.temp_mpu);
        memcpy(&frame_buf[offset_frame], &mpu.pressure, sizeof(mpu.pressure));
        offset_frame += sizeof(mpu.pressure);
        memcpy(&frame_buf[offset_frame], &mpu.temp_bmp, sizeof(mpu.temp_bmp));
        offset_frame += sizeof(mpu.temp_bmp);

    #endif

    #if USE_INA226
        if (ina_queue != NULL && uxQueueMessagesWaiting(ina_queue) > 0) {
            int32_t sh=0, bu=0, cu=0, po=0;
            count = 0;
            while (xQueueReceive(ina_queue, &ina_temp, 0) == pdTRUE) {
                sh += ina_temp.shunt;
                bu += ina_temp.bus;
                cu += ina_temp.current;
                po += ina_temp.power;
                count++;
            }
            if (count > 0) {
                ina.shunt   = sh / count;
                ina.bus     = bu / count;
                ina.current = cu / count;
                ina.power   = po / count;
            }
        }
        print_ina(&ina);
        memcpy(&frame_buf[offset_frame], &ina.bus, sizeof(ina.bus));
        offset_frame += sizeof(ina.bus);
        memcpy(&frame_buf[offset_frame], &ina.current, sizeof(ina.current));
        offset_frame += sizeof(ina.current);
        memcpy(&frame_buf[offset_frame], &ina.power, sizeof(ina.power));
        offset_frame += sizeof(ina.power);
        memcpy(&frame_buf[offset_frame], &ina.shunt, sizeof(ina.shunt));
        offset_frame += sizeof(ina.shunt);
    #endif

    #if USE_UDP
        udp_msg_t msg;
        memcpy(msg.data, frame_buf, frame_size);
        msg.len = frame_size;
        send_udp_msg(&msg);
    #endif

        vTaskDelay(pdMS_TO_TICKS(MONITOR_PERIOD));
    }
    return;
}

//maybe timerTask is better?

esp_err_t start_monitoring_task() {

    if (monitoring) {
        return ESP_ERR_INVALID_STATE;
    }
    monitoring = true;
    BaseType_t res = xTaskCreate(monitoring_task, "sensors_monitoring", 8196, NULL, 5, NULL);
    if (res != pdPASS) {
        log_msg(TAG, "Error (%d) create monitoring task", res);
        return ESP_ERR_NOT_ALLOWED;
    }
    log_msg(TAG, "Monitoring task created");
    return ESP_OK;
}