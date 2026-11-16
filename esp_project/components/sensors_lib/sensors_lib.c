#include "sensors_lib.h"
#include "log_lib.h"
#include <string.h>

// Every sensor's public init function.
#include "hcsr04.h"
#include "ina226.h"
#include "ky003.h"
#include "mpu9250.h"
#include "bmp280.h"
#include "rfid_rc522.h"
#include "rcwl_0515.h"
#include "vl53l1x.h"
#include "as5600.h"
#include "ky035.h"
#include "fc33.h"
#include "ky033.h"
#include "ky002.h"
#include "ky040.h"
#include "dht11.h"
#include "ky020.h"
#include "ky018.h"
#include "ky031.h"
#include "ky017.h"
#include "ky005.h"
#include "ky022.h"
#include "ky021.h"
#include "ky004.h"
#include "ky039.h"
#include "ky032.h"
#include "ky023.h"
#include "ds18b20.h"

static const char *TAG = "sensors_library";

// Guards against double-initialization (matches the original firmware's
// `monitoring` flag). Individual sensor tasks currently loop unconditionally
// once started; a full stop_sensors() that signals every task to exit would
// require each sensor's task loop to check a shared flag — not implemented
// yet, tell me if you need it and I'll wire it through every sensor file.
static bool sensors_initialized = false;

esp_err_t serialize_header(const header_sensor_t *hd, uint8_t *buf) {
    if (hd == NULL || buf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    buf[0] = hd->type;
    buf[1] = hd->esp_id;
    memcpy(&buf[2], &hd->timestamp, sizeof(uint32_t)); // little-endian
    return ESP_OK;
}

// Attempts every sensor's init unconditionally; each returns
// ESP_ERR_NOT_SUPPORTED when its own CONFIG_USE_xxx is disabled, so no
// #if guards are needed here — this keeps the orchestrator itself simple
// and centralizes the enable/disable logic in each sensor's own file.
esp_err_t init_sensors(void) {
    if (sensors_initialized) {
        log_msg_lvl(ESP_LOG_WARN, TAG, "Sensors already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    sensors_initialized = true;

    struct { const char *name; esp_err_t (*init_fn)(void); } sensors[] = {
        { "HCSR04 (front+rear)", init_hcsr04 },
        { "INA226",         init_ina226 },
        { "KY003",          init_ky003 },
        { "MPU9250",        init_mpu9250 },
        { "BMP280",         init_bmp280 },
        { "RFID RC522",     init_rfid_rc522 },
        { "RCWL-0515",      init_rcwl_0515 },
        { "VL53L1X",        init_vl53l1x },
        { "AS5600",         init_as5600 },
        { "KY035",          init_ky035 },
        { "FC-33",          init_fc33 },
        { "KY033",          init_ky033 },
        { "KY002",          init_ky002 },
        { "KY040",          init_ky040 },
        { "DHT11",          init_dht11 },
        { "KY020",          init_ky020 },
        { "KY018",          init_ky018 },
        { "KY031",          init_ky031 },
        { "KY017",          init_ky017 },
        { "KY005",          init_ky005 },
        { "KY022",          init_ky022 },
        { "KY021",          init_ky021 },
        { "KY004",          init_ky004 },
        { "KY039",          init_ky039 },
        { "KY032",          init_ky032 },
        { "KY023",          init_ky023 },
        { "DS18B20",        init_ds18b20 },
    };

    for (size_t i = 0; i < sizeof(sensors) / sizeof(sensors[0]); i++) {
        esp_err_t err = sensors[i].init_fn();
        if (err == ESP_OK) {
            log_msg(TAG, "%s enabled and started", sensors[i].name);
        } else if (err != ESP_ERR_NOT_SUPPORTED) {
            log_msg(TAG, "Error (%s) starting %s", esp_err_to_name(err), sensors[i].name);
        }
    }

    return ESP_OK;
}