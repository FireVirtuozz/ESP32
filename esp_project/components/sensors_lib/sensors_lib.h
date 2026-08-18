#ifndef SENSORS_LIB_H_
#define SENSORS_LIB_H_

#include <inttypes.h>
#include <stdbool.h>
#include <esp_err.h>

// Telemetry frame header shared by every sensor. Wire layout (little-endian):
// [0] = type, [1] = esp_id, [2..5] = timestamp (uint32_t).
// This layout is relied upon by the PC-side receiver: do not reorder fields
// or renumber sensor_type_t values without updating it too.
#define HEADER_SENSOR_SIZE (sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint32_t))

typedef struct header_sensor_st {
    uint8_t type;
    uint8_t esp_id;
    uint32_t timestamp;
} header_sensor_t;

typedef enum {
    SENSOR_TYPE_UNKNOWN    = 0,
    SENSOR_TYPE_HCSR04     = 1,
    SENSOR_TYPE_INA226     = 2,
    SENSOR_TYPE_KY003      = 3,
    SENSOR_TYPE_MPU9250    = 4,
    SENSOR_TYPE_RFID_RC522 = 5,
    SENSOR_TYPE_RCWL_0515  = 6,
    SENSOR_TYPE_VL53L1X    = 7,
    SENSOR_TYPE_AS5600     = 8,
    SENSOR_TYPE_KY035      = 9,
    SENSOR_TYPE_FC33       = 10,
    SENSOR_TYPE_KY033      = 11,
    SENSOR_TYPE_KY002      = 12,
    SENSOR_TYPE_KY040      = 13,
    SENSOR_TYPE_DHT11      = 14,
    SENSOR_TYPE_KY020      = 15,
    SENSOR_TYPE_KY018      = 16,
    SENSOR_TYPE_KY031      = 17,
    SENSOR_TYPE_KY017      = 18,
    SENSOR_TYPE_KY005      = 19,
    SENSOR_TYPE_KY022      = 20,
    SENSOR_TYPE_KY021      = 21,
    SENSOR_TYPE_KY004      = 22,
    SENSOR_TYPE_KY039      = 23,
    SENSOR_TYPE_KY032      = 24,
    SENSOR_TYPE_KY023_XY   = 25,
    SENSOR_TYPE_KY023_SW   = 26,
    SENSOR_TYPE_ESP        = 27,
    SENSOR_TYPE_PING       = 28,
    SENSOR_TYPE_MOTOR      = 29,
    SENSOR_TYPE_BREAK      = 30,
    SENSOR_TYPE_BMP        = 31,
    SENSOR_TYPE_DS18B20    = 32,

    SENSOR_TYPE_MAX
} sensor_type_t;

/**
 * Pack a telemetry header into the first HEADER_SENSOR_SIZE bytes of buf.
 */
esp_err_t serialize_header(const header_sensor_t *hd, uint8_t *buf);

/**
 * Initialize and start every sensor enabled via Kconfig (CONFIG_USE_xxx),
 * each in its own FreeRTOS task.
 */
esp_err_t init_sensors(void);

// --- Cross-component accessors (consumed by actuators_lib's h_bridge) ---

/**
 * Wheel-encoder pulse count over the last 20ms window (KY033).
 */
esp_err_t get_pulses_count_20ms(uint16_t *count);

/**
 * Wheel-encoder pulse count over the last 100ms sliding window (KY033),
 * refreshed every 20ms rather than only every 100ms.
 */
esp_err_t get_pulses_count_100ms(uint16_t *count);

/**
 * Whether the front HC-SR04 currently reports an obstacle close enough to
 * block forward motion.
 */
esp_err_t get_front_blocked(bool *blocked);

/**
 * Whether the rear HC-SR04 currently reports an obstacle close enough to
 * block backward motion.
 */
esp_err_t get_rear_blocked(bool *blocked);

#endif // SENSORS_LIB_H_