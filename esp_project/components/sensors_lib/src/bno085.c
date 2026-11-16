#include "bno085.h"
#include "sensors_lib.h"
#include "peripherals/i2c_helper.h"
#include "log_lib.h"
#include <string.h>

#if CONFIG_USE_UDPLIB
#include "udp_lib.h"
#endif

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include "sh2.h"
#include "sh2_err.h"
#include "sh2_SensorValue.h"
#include "euler.h"

static const char *TAG = "bno085_sensor";

#if CONFIG_USE_BNO085

#define BNO085_RESET_SETTLE_MS   250   // margin after releasing RESET, not a verified datasheet value -
                                        // the real "ready" signal is the SH2_RESET event below, not this delay
#define BNO085_OPEN_TIMEOUT_MS   1000
#define BNO085_SERVICE_PERIOD_MS 10
#define DEBUG_BNO085 1

static i2c_master_dev_handle_t dev;
static volatile bool reset_seen;
static volatile bool cal_saved = false;

// --- sh2 HAL glue -----------------------------------------------------
// sh2_hal.h contract: open() must reset the device into a known state;
// read()/write() move raw SHTP bytes with no register-address framing at
// all (unlike every other I2C sensor in this project - see i2c_bus_*_raw
// in i2c_helper.c).

static int hal_open(sh2_Hal_t *self) {
    (void)self;
    esp_err_t probe_err;

    gpio_reset_pin(BNO085_RESET_GPIO);
    gpio_set_direction(BNO085_RESET_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BNO085_RESET_GPIO, 0);

    gpio_reset_pin(BNO085_INT_GPIO);
    gpio_set_direction(BNO085_INT_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BNO085_INT_GPIO, GPIO_PULLUP_ONLY);

    if (i2c_bus_add_device(BNO085_I2C_ADDR, 400000, &dev) != ESP_OK) {
        return -1;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(BNO085_RESET_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(BNO085_RESET_SETTLE_MS));
     
    // Actively probe until the device answers, instead of a blind fixed
    // delay - this also happens to nudge the I2C bus out of any stuck state
    // left over from before reset (each probe toggles SDA/SCL even on a
    // NACK). Removing this used to make the sensor fail to connect - a
    // fixed vTaskDelay alone wasn't reliably enough margin.
    probe_err = ESP_FAIL;
    for (int retry = 0; retry < 20 && probe_err != ESP_OK; retry++) {
        vTaskDelay(pdMS_TO_TICKS(20));
        probe_err = i2c_bus_probe(BNO085_I2C_ADDR, 20);
    }
    log_msg(TAG, "I2C probe @0x%02X: %s", BNO085_I2C_ADDR, esp_err_to_name(probe_err));

#if DEBUG_BNO085
    // Diagnostic: is anything even alive on the bus, now that RESET has
    // actually been released and settled? Isolates wiring/address/PS0-PS1
    // from protocol bugs. Must run AFTER releasing reset, not before.
    i2c_bus_scan();
    probe_err = i2c_bus_probe(BNO085_I2C_ADDR, 100);
    log_msg(TAG, "I2C probe @0x%02X: %s", BNO085_I2C_ADDR, esp_err_to_name(probe_err));
#endif
    return 0;
}

static void hal_close(sh2_Hal_t *self) {
    (void)self;
    gpio_set_level(BNO085_RESET_GPIO, 0);
}

// H_INTN is active-low: only attempt the I2C transaction once it's
// asserted, otherwise you risk a NACK or an I2C timeout from the sensor's
// clock stretching (community-confirmed failure mode, see bno085.h).
static int hal_read(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len, uint32_t *t_us) {
    (void)self;
    esp_err_t err;

    int int_level = gpio_get_level(BNO085_INT_GPIO);
    if (int_level != 0) {
        //log_msg_lvl(ESP_LOG_ERROR, TAG, "GPIO INT: Not ready");
        return 0; // no data ready, cheap GPIO check, no bus traffic
    }


    // SHTP header is 4 bytes: 2-byte length (top bit = continuation flag)
    // + 2-byte channel/seq. Peek it first to learn the real packet length,
    // then re-read the whole thing (header included) in one transaction -
    // this two-step pattern is the community-verified way to handle
    // variable-length SHTP transfers over I2C.
    uint8_t header[4];
    err = i2c_bus_read_raw(dev, header, sizeof(header));
    //log_msg(TAG, "hal_read: INT asserted, header read %s: %02X %02X %02X %02X",
    //    esp_err_to_name(err), header[0], header[1], header[2], header[3]);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed to read header (%s)", esp_err_to_name(err));
        return 0;
    }

    uint16_t packet_len = ((uint16_t)header[1] << 8 | header[0]) & 0x7FFF;
    //log_msg(TAG, "hal_read: packet_len=%u (buffer len=%u)", packet_len, len);
    if (packet_len == 0 || packet_len > len) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Packet len Error (len: %d, head_len: %d)", len, packet_len);
        return 0;
    }

    err = i2c_bus_read_raw(dev, pBuffer, packet_len);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed to read buffer (%s)", esp_err_to_name(err));
        return 0;
    }

    *t_us = (uint32_t)esp_timer_get_time();
    return packet_len;
}

static int hal_write(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len) {
    (void)self;
    esp_err_t err = i2c_bus_write_raw(dev, pBuffer, len);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed to write buffer (%s)", esp_err_to_name(err));
    }
    return err == ESP_OK ? (int)len : 0;
}

static uint32_t hal_getTimeUs(sh2_Hal_t *self) {
    (void)self;
    return (uint32_t)esp_timer_get_time();
}

static sh2_Hal_t hal = {
    .open = hal_open,
    .close = hal_close,
    .read = hal_read,
    .write = hal_write,
    .getTimeUs = hal_getTimeUs,
};

static char* reset_cause_to_str(uint8_t reset_cause) {
    switch (reset_cause)
    {
    case 0: return "Not applicable";
    case 1: return "Power On Reset";
    case 2: return "Internal System Reset";
    case 3: return "Watchdog Timeout";
    case 4: return "External Reset";
    case 5: return "Other";
    default: return "Unknown";
    }
}

static void print_product_id(sh2_ProductIds_t *prod) {
    log_msg(TAG, "Products ID: num %u", prod->numEntries);
    for (uint8_t i=0; i < prod->numEntries; i++) {
        log_msg(TAG, "reset cause: %s", reset_cause_to_str(prod->entry[i].resetCause));
        log_msg(TAG, "Software build number: %u", prod->entry[i].swBuildNumber);
        log_msg(TAG, "Software part number: %u", prod->entry[i].swPartNumber);
        log_msg(TAG, "Software version: %u.%u.%u", 
            prod->entry[i].swVersionMajor, prod->entry[i].swVersionMinor, prod->entry[i].swVersionPatch);
    }
}

static char* oscillator_type_to_str(sh2_OscType_t osc) {
    switch (osc)
    {
    case SH2_OSC_INTERNAL: return "Internal";
    case SH2_OSC_EXT_CRYSTAL: return "Extern crystal";
    case SH2_OSC_EXT_CLOCK: return "Extern clock";
    default: return "Unknown";
    }
}

// --- sh2 callbacks ------------------------------------------------------

#define CLASSIF_SIZE 2
#define ACCEL_SIZE 17
#define LINEAR_ACCEL_SIZE 17
#define ROTATION_SIZE 17

static void serialize_acceleration(sh2_SensorValue_t *value, uint8_t *buf) {
    buf[0] = value->status;
    uint32_t timestamp = (uint32_t)(value->timestamp);
    memcpy(&buf[1], &timestamp, sizeof(uint32_t));
    memcpy(&buf[5], &value->un.accelerometer.x, sizeof(float));
    memcpy(&buf[9], &value->un.accelerometer.y, sizeof(float));
    memcpy(&buf[13], &value->un.accelerometer.z, sizeof(float));
}

static void serialize_linear_acceleration(sh2_SensorValue_t *value, uint8_t *buf) {
    buf[0] = value->status;
    uint32_t timestamp = (uint32_t)(value->timestamp);
    memcpy(&buf[1], &timestamp, sizeof(uint32_t));
    memcpy(&buf[5], &value->un.linearAcceleration.x, sizeof(float));
    memcpy(&buf[9], &value->un.linearAcceleration.y, sizeof(float));
    memcpy(&buf[13], &value->un.linearAcceleration.z, sizeof(float));
}

static void serialize_classif(sh2_SensorValue_t *value, uint8_t *buf) {
    buf[0] = value->status;
    buf[1] = value->un.stabilityClassifier.classification;
}

typedef struct rotation_st {
    float roll;
    float pitch;
    float yaw;
} rotation_t;

static void serialize_rotation(const sh2_SensorValue_t * restrict value, const rotation_t * restrict rot, uint8_t * restrict buf) {
    buf[0] = value->status;
    memcpy(&buf[1], &value->un.rotationVector.accuracy, sizeof(float));
    memcpy(&buf[5], &rot->roll, sizeof(float));
    memcpy(&buf[9], &rot->pitch, sizeof(float));
    memcpy(&buf[13], &rot->yaw, sizeof(float));
}

static const char* shtp_event_to_str(sh2_ShtpEvent_t event) {
    switch (event) {
        case SH2_SHTP_TX_DISCARD:         return "SH2_SHTP_TX_DISCARD";
        case SH2_SHTP_SHORT_FRAGMENT:      return "SH2_SHTP_SHORT_FRAGMENT";
        case SH2_SHTP_TOO_LARGE_PAYLOADS:  return "SH2_SHTP_TOO_LARGE_PAYLOADS";
        case SH2_SHTP_BAD_RX_CHAN:         return "SH2_SHTP_BAD_RX_CHAN";
        case SH2_SHTP_BAD_TX_CHAN:         return "SH2_SHTP_BAD_TX_CHAN";
        case SH2_SHTP_BAD_FRAGMENT:        return "SH2_SHTP_BAD_FRAGMENT";
        case SH2_SHTP_BAD_SN:              return "SH2_SHTP_BAD_SN";
        case SH2_SHTP_INTERRUPTED_PAYLOAD: return "SH2_SHTP_INTERRUPTED_PAYLOAD";
        default:                           return "UNKNOWN_SHTP_EVENT";
    }
}

static void event_callback(void *cookie, sh2_AsyncEvent_t *event) {
    (void)cookie;

    switch (event->eventId)
    {
    case SH2_RESET:
        reset_seen = true;
        log_msg(TAG, "BNO085 reset complete");
        break;
    case SH2_SHTP_EVENT:
        sh2_ShtpEvent_t shtp_err = event->shtpEvent;
        //log_msg(TAG, "SHTP Error (%s)", shtp_event_to_str(shtp_err));
        break;
    default:
        break;
    }
}

static void sensor_callback(void *cookie, sh2_SensorEvent_t *event) {
    (void)cookie;
 
    sh2_SensorValue_t value;
    if (sh2_decodeSensorEvent(&value, event) != SH2_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed to decode sensor event");
        return;
    }
 
    static const char *status_str[] = { "unreliable", "low", "medium", "high" };
    const char *status_name = value.status < 4 ? status_str[value.status] : "?";
    const float RAD_TO_DEG = 57.2958f;

    //log_msg(TAG, "sensor event: sensorId=0x%02X", value.sensorId);
#if CONFIG_ACTIVATE_ROTATION
    if (value.sensorId == SH2_ROTATION_VECTOR) {

        // First time the fusion reaches high confidence this session, persist
        // it to FRS so next boot starts from this calibration instead of
        // unreliable-from-scratch. Only fires once - sh2_saveDcdNow() writes
        // flash, not something to call every report.
        if (value.status == 3 && !cal_saved) {
            cal_saved = true;
            int rc = sh2_saveDcdNow();
            log_msg(TAG, "Calibration reached high, saved to FRS: %s", rc == SH2_OK ? "OK" : "FAILED");
        }

        float roll, pitch, yaw;
        rotation_t rot;
        q_to_ypr(value.un.rotationVector.real, value.un.rotationVector.i,
                 value.un.rotationVector.j, value.un.rotationVector.k,
                 &rot.roll, &rot.pitch, &rot.yaw);
 
        //log_msg(TAG, "Yaw=%.1f Pitch=%.1f Roll=%.1f deg (accuracy=%.1f deg, status=%s)",
        //    rot.yaw * RAD_TO_DEG, rot.pitch * RAD_TO_DEG, rot.roll * RAD_TO_DEG,
        //    value.un.rotationVector.accuracy * RAD_TO_DEG, status_name);
 
        header_sensor_t header = {0};
        header.esp_id = (uint8_t)CONFIG_ESP_ID;
        header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
        header.type = SENSOR_TYPE_BNO085_ROT;
 
        uint8_t buf[HEADER_SENSOR_SIZE + ROTATION_SIZE];
        serialize_header(&header, buf);
        serialize_rotation(&value, &rot, &buf[HEADER_SENSOR_SIZE]);
 
#if CONFIG_USE_UDPLIB
        send_udp_sensor(buf, sizeof(buf));
#endif
        return;
    }
#endif
#if CONFIG_ACTIVATE_ACCELERATION
    if (value.sensorId == SH2_ACCELEROMETER) {
        // Calibrated acceleration, gravity INCLUDED (datasheet 2.1.1) - if
        // you want gravity removed for impact/motion detection instead,
        // that's SH2_LINEAR_ACCELERATION, a separate sensor to enable.
    /*
        log_msg(TAG, "Accel x=%.2f y=%.2f z=%.2f m/s^2 (status=%s)",
            value.un.accelerometer.x, value.un.accelerometer.y,
            value.un.accelerometer.z, status_name);
            */
 
        header_sensor_t header = {0};
        header.esp_id = (uint8_t)CONFIG_ESP_ID;
        header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
        header.type = SENSOR_TYPE_BNO085_ACCEL;

        uint8_t buf[HEADER_SENSOR_SIZE + ACCEL_SIZE];
        serialize_header(&header, buf);
        serialize_acceleration(&value, &buf[HEADER_SENSOR_SIZE]);

#if CONFIG_USE_UDPLIB
        send_udp_sensor(buf, sizeof(buf));
#endif
        return;
    }
#endif
#if CONFIG_ACTIVATE_CLASSIFICATION
    if (value.sensorId == SH2_STABILITY_CLASSIFIER) {
        static const char *classif_str[] = { "unknown", "on_table", "stationary", "stable", "motion" };
        uint8_t classif = value.un.stabilityClassifier.classification;
        const char *classif_name = classif < 5 ? classif_str[classif] : "?";
 
        //log_msg(TAG, "Stability: %s (status=%s)", classif_name, status_name);
 
        header_sensor_t header = {0};
        header.esp_id = (uint8_t)CONFIG_ESP_ID;
        header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
        header.type = SENSOR_TYPE_BNO085_STABILITY;
 
        // Wire format: classification (u8, 0=unknown..4=motion, see
        // STABILITY_CLASSIFIER_* defines in sh2_SensorValue.h).
        uint8_t buf[HEADER_SENSOR_SIZE + CLASSIF_SIZE];
        serialize_header(&header, buf);
        serialize_classif(&value, &buf[HEADER_SENSOR_SIZE]);
 
#if CONFIG_USE_UDPLIB
        send_udp_sensor(buf, sizeof(buf));
#endif
        return;
    }
#endif
#if CONFIG_ACTIVATE_LINEAR_ACCELERATION
    if (value.sensorId == SH2_LINEAR_ACCELERATION) {
        // Same units/struct as SH2_ACCELEROMETER, gravity removed - reads
        // ~0 at rest, spikes on real motion/impact/braking.

        //log_msg(TAG, "LinAccel x=%.2f y=%.2f z=%.2f m/s^2 (status=%s)",
        //    value.un.linearAcceleration.x, value.un.linearAcceleration.y,
        //    value.un.linearAcceleration.z, status_name);
 
        header_sensor_t header = {0};
        header.esp_id = (uint8_t)CONFIG_ESP_ID;
        header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
        header.type = SENSOR_TYPE_BNO085_LINEAR_ACCEL;
 
        uint8_t buf[HEADER_SENSOR_SIZE + LINEAR_ACCEL_SIZE];
        serialize_header(&header, buf);
        serialize_linear_acceleration(&value, &buf[HEADER_SENSOR_SIZE]);
 
#if CONFIG_USE_UDPLIB
        send_udp_sensor(buf, sizeof(buf));
#endif
        return;
    }
#endif
}

// --- task ---------------------------------------------------------------

static void bno085_task(void *params) {
    (void)params;

    reset_seen = false;
    if (sh2_open(&hal, event_callback, NULL) != SH2_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "sh2_open failed");
        vTaskDelete(NULL);
        return;
    }

    // Wait for the real reset-complete signal, not a fixed delay - mirrors
    // the fix we made for the VL53L1X boot check. Note: sh2_open() itself
    // processes the initial SHTP handshake internally and may already have
    // fired the SH2_RESET event before returning - that's why reset_seen is
    // cleared BEFORE calling it, not after.
    uint32_t waited_ms = 0;
    while (!reset_seen && waited_ms < BNO085_OPEN_TIMEOUT_MS) {
        sh2_service();
        if (waited_ms % 100 == 0) {
            log_msg(TAG, "waiting for reset... INT level=%d", gpio_get_level(BNO085_INT_GPIO));
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        waited_ms += 10;
    }
    if (!reset_seen) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Timed out waiting for SH2_RESET");
        sh2_close();
        vTaskDelete(NULL);
        return;
    }

    sh2_setSensorCallback(sensor_callback, NULL);

    // Explicitly enable dynamic calibration on all three physical sensors -
    // usually on by default, but forced here so we're not relying on that.
    if (sh2_setCalConfig(SH2_CAL_ACCEL | SH2_CAL_GYRO | SH2_CAL_MAG) != SH2_OK) {
        log_msg_lvl(ESP_LOG_WARN, TAG, "Failed to set calibration config");
    }

    // Axis reorientation: the physical mounting on this board cyclically
    // permutes the axes (roll->yaw->pitch->roll observed empirically), a
    // 120deg rotation about the (1,1,1) diagonal. Two possible cycle
    // directions exist - test which one is correct with a clean single-axis
    // rotation, swap to the other quaternion below if it comes out backwards.
    sh2_Quaternion_t reorientation = { .x = -0.5, .y = -0.5, .z = -0.5, .w = 0.5 };
    // sh2_Quaternion_t reorientation = { .x = -0.5, .y = -0.5, .z = -0.5, .w = 0.5 }; // other direction
    //if (sh2_setReorientation(&reorientation) != SH2_OK) {
    //    log_msg_lvl(ESP_LOG_WARN, TAG, "Failed to set reorientation, axes will stay as-is");
    //}

    #define PACE_AFTER_CONFIG() do { \
        for (int _i = 0; _i < 3; _i++) { sh2_service(); vTaskDelay(pdMS_TO_TICKS(20)); } \
    } while (0)

    sh2_SensorConfig_t config = {0};
    config.reportInterval_us = BNO085_REPORT_INTERVAL_US;

#if CONFIG_ACTIVATE_ROTATION
    if (sh2_setSensorConfig(SH2_ROTATION_VECTOR, &config) != SH2_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed to enable rotation vector");
        sh2_close();
        vTaskDelete(NULL);
        return;
    }
    PACE_AFTER_CONFIG();
#endif
#if CONFIG_ACTIVATE_ACCELERATION
    if (sh2_setSensorConfig(SH2_ACCELEROMETER, &config) != SH2_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed to enable accelerometer");
        sh2_close();
        vTaskDelete(NULL);
        return;
    }
    PACE_AFTER_CONFIG();
#endif
#if CONFIG_ACTIVATE_CLASSIFICATION
    if (sh2_setSensorConfig(SH2_STABILITY_CLASSIFIER, &config) != SH2_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed to enable stability classifier");
        sh2_close();
        vTaskDelete(NULL);
        return;
    }
    PACE_AFTER_CONFIG();
#endif
#if CONFIG_ACTIVATE_LINEAR_ACCELERATION
    if (sh2_setSensorConfig(SH2_LINEAR_ACCELERATION, &config) != SH2_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed to enable linear acceleration");
        sh2_close();
        vTaskDelete(NULL);
        return;
    }
    PACE_AFTER_CONFIG();
#endif

    log_msg(TAG, "BNO085 initialized at address 0x%02X (RESET: GPIO%d, INT: GPIO%d)",
        BNO085_I2C_ADDR, BNO085_RESET_GPIO, BNO085_INT_GPIO);

#if DEBUG_BNO085
    sh2_ProductIds_t prod;   
    if (sh2_getProdIds(&prod) != SH2_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed to get product ID");
    } else {
        print_product_id(&prod);
    }

    sh2_OscType_t osc;
    if (sh2_getOscType(&osc) != SH2_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed to get oscillator type");
    } else {
        log_msg(TAG, "Oscillator type: %s", oscillator_type_to_str(osc));
    }

    /*
    sh2_SensorConfig_t sensor_cfg;
    if (sh2_getSensorConfig(SH2_ACCELEROMETER, &sensor_cfg) != SH2_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed to get sensor config");
    } else {
        log_msg(TAG, "Oscillator type: %s", oscillator_type_to_str(osc));
    }
    

    sh2_Counts_t counts;
    if (sh2_getCounts(SH2_ACCELEROMETER, &counts) != SH2_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed to get sensor config");
    } else {
        log_msg(TAG, "Oscillator type: %s", oscillator_type_to_str(osc));
    }

    uint8_t sensors_mask;
    if (sh2_getCalConfig(&sensors_mask) != SH2_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed to get calibration config");
    } else {
        log_msg(TAG, "Oscillator type: %s", oscillator_type_to_str(osc));
    }

    sh2_SensorMetadata_t metadata;
    if (sh2_getMetadata(SH2_ACCELEROMETER, &metadata) != SH2_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Failed to get metadata");
    } else {
        log_msg(TAG, "Oscillator type: %s", oscillator_type_to_str(osc));
    }
    */
#endif

    while (true) {
        sh2_service(); // pumps hal_read/hal_write, dispatches callbacks above
        vTaskDelay(pdMS_TO_TICKS(BNO085_SERVICE_PERIOD_MS));
    }
}

esp_err_t init_bno085(void) {
    return xTaskCreate(bno085_task, "bno085_task", 4096, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

#else // !CONFIG_USE_BNO085

esp_err_t init_bno085(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_BNO085



// TO DISCOVER
//sh2_finishCal / sh2_startCal
//sh2_clearDcdAndReset / sh2_setDcdAutoSave
//sh2_flush
//sh2_reinitialize
//sh2_persistTare / sh2_clearTare / sh2_setTareNow
//sh2_getErrors
//sh2_getFrs / sh2_setFrs
//sh2_setSensorConfig
//sh2_devSleep / sh2_devOn / sh2_devReset