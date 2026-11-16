#include "lidar.h"
#include "peripherals/uart_helper.h"
#include "log_lib.h"
#include "sensors_lib.h"
#include <string.h>

static const char *TAG = "lidar_driver";

#if CONFIG_USE_LIDAR

#include "udp_lib.h"
#include "esp_timer.h"

#define LIDAR_UART_PORT   UART_NUM_1
#define LIDAR_BAUD_RATE   230400
#define LIDAR_RX_BUF_SIZE   2048
#define LIDAR_HEADER      0x54
#define LIDAR_VERLEN      0x2C   // type=1 (upper 3 bits) | 12 points (lower 5 bits)
#define LIDAR_PACKET_LEN  47     // header+verlen+speed+angles+12*3pts+timestamp+crc, CRC excluded from the 44 covered below
#define LIDAR_CRC_COVERED_LEN 46 // CRC8 is computed over bytes [0..45], byte 46 is the CRC itself
#define LIDAR_BATCH_SIZE 20
#define LIDAR_POINTS_PER_PACKET 12

static uart_bus_t lidar_bus = {0};

typedef struct {
    uint16_t distance_mm;
    uint8_t  intensity;
} lidar_point_t;

typedef struct {
    uint16_t speed_deg_s;
    uint16_t start_angle_centideg; // angle ×100
    uint16_t end_angle_centideg;
    uint16_t timestamp_ms;
    lidar_point_t points[LIDAR_POINTS_PER_PACKET];
} lidar_packet_t;

static const uint8_t crc_table[256] = {
    0x00, 0x4d, 0x9a, 0xd7, 0x79, 0x34, 0xe3, 0xae, 0xf2, 0xbf, 0x68, 0x25,
    0x8b, 0xc6, 0x11, 0x5c, 0xa9, 0xe4, 0x33, 0x7e, 0xd0, 0x9d, 0x4a, 0x07,
    0x5b, 0x16, 0xc1, 0x8c, 0x22, 0x6f, 0xb8, 0xf5, 0x1f, 0x52, 0x85, 0xc8,
    0x66, 0x2b, 0xfc, 0xb1, 0xed, 0xa0, 0x77, 0x3a, 0x94, 0xd9, 0x0e, 0x43,
    0xb6, 0xfb, 0x2c, 0x61, 0xcf, 0x82, 0x55, 0x18, 0x44, 0x09, 0xde, 0x93,
    0x3d, 0x70, 0xa7, 0xea, 0x3e, 0x73, 0xa4, 0xe9, 0x47, 0x0a, 0xdd, 0x90,
    0xcc, 0x81, 0x56, 0x1b, 0xb5, 0xf8, 0x2f, 0x62, 0x97, 0xda, 0x0d, 0x40,
    0xee, 0xa3, 0x74, 0x39, 0x65, 0x28, 0xff, 0xb2, 0x1c, 0x51, 0x86, 0xcb,
    0x21, 0x6c, 0xbb, 0xf6, 0x58, 0x15, 0xc2, 0x8f, 0xd3, 0x9e, 0x49, 0x04,
    0xaa, 0xe7, 0x30, 0x7d, 0x88, 0xc5, 0x12, 0x5f, 0xf1, 0xbc, 0x6b, 0x26,
    0x7a, 0x37, 0xe0, 0xad, 0x03, 0x4e, 0x99, 0xd4, 0x7c, 0x31, 0xe6, 0xab,
    0x05, 0x48, 0x9f, 0xd2, 0x8e, 0xc3, 0x14, 0x59, 0xf7, 0xba, 0x6d, 0x20,
    0xd5, 0x98, 0x4f, 0x02, 0xac, 0xe1, 0x36, 0x7b, 0x27, 0x6a, 0xbd, 0xf0,
    0x5e, 0x13, 0xc4, 0x89, 0x63, 0x2e, 0xf9, 0xb4, 0x1a, 0x57, 0x80, 0xcd,
    0x91, 0xdc, 0x0b, 0x46, 0xe8, 0xa5, 0x72, 0x3f, 0xca, 0x87, 0x50, 0x1d,
    0xb3, 0xfe, 0x29, 0x64, 0x38, 0x75, 0xa2, 0xef, 0x41, 0x0c, 0xdb, 0x96,
    0x42, 0x0f, 0xd8, 0x95, 0x3b, 0x76, 0xa1, 0xec, 0xb0, 0xfd, 0x2a, 0x67,
    0xc9, 0x84, 0x53, 0x1e, 0xeb, 0xa6, 0x71, 0x3c, 0x92, 0xdf, 0x08, 0x45,
    0x19, 0x54, 0x83, 0xce, 0x60, 0x2d, 0xfa, 0xb7, 0x5d, 0x10, 0xc7, 0x8a,
    0x24, 0x69, 0xbe, 0xf3, 0xaf, 0xe2, 0x35, 0x78, 0xd6, 0x9b, 0x4c, 0x01,
    0xf4, 0xb9, 0x6e, 0x23, 0x8d, 0xc0, 0x17, 0x5a, 0x06, 0x4b, 0x9c, 0xd1,
    0x7f, 0x32, 0xe5, 0xa8
};

static uint8_t crc8_calc(const uint8_t *data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc = crc_table[(crc ^ data[i]) & 0xFF];
    }
    return crc;
}

#define LIDAR_PACKET_SERIALIZED_SIZE \
    (2 * sizeof(uint16_t) /* speed, start_angle */ \
     + LIDAR_POINTS_PER_PACKET * (sizeof(uint16_t) + sizeof(uint8_t)) /* distance+intensity par point */ \
     + 2 * sizeof(uint16_t) /* end_angle, timestamp */)

static void serialize_lidar_packet(const lidar_packet_t *pkt, uint8_t *buf) {
    uint16_t len = 0;

    memcpy(&buf[len], &pkt->speed_deg_s, sizeof(uint16_t));
    len += sizeof(uint16_t);

    memcpy(&buf[len], &pkt->start_angle_centideg, sizeof(uint16_t));
    len += sizeof(uint16_t);

    for (int i = 0; i < LIDAR_POINTS_PER_PACKET; i++) {
        memcpy(&buf[len], &pkt->points[i].distance_mm, sizeof(uint16_t));
        len += sizeof(uint16_t);

        memcpy(&buf[len], &pkt->points[i].intensity, sizeof(uint8_t));
        len += sizeof(uint8_t);
    }

    memcpy(&buf[len], &pkt->end_angle_centideg, sizeof(uint16_t));
    len += sizeof(uint16_t);

    memcpy(&buf[len], &pkt->timestamp_ms, sizeof(uint16_t));
}

// Byte-by-byte resynchronization: reads one byte at a time until it finds
// a header that's actually followed by the fixed VerLen value - a lone
// 0x54 match isn't enough, since any random data byte in a corrupted
// stream could equal 0x54 by chance. This lets the parser recover on its
// own after a dropped/corrupted byte, instead of staying permanently
// misaligned with the stream.
static esp_err_t lidar_find_header(void) {
    uint8_t byte;
    for (int attempts = 0; attempts < LIDAR_PACKET_LEN * 2; attempts++) {
        int n = uart_bus_read(&lidar_bus, &byte, 1, 100);
        if (n <= 0) {
            return ESP_ERR_TIMEOUT;
        }
        if (byte == LIDAR_HEADER) {
            uint8_t verlen;
            n = uart_bus_read(&lidar_bus, &verlen, 1, 100);
            if (n > 0 && verlen == LIDAR_VERLEN) {
                return ESP_OK; // header confirmed, stream now aligned
            }
            // False match (0x54 appeared in the data, not a real header) -
            // keep scanning from here, don't restart the whole loop.
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t lidar_read_packet(lidar_packet_t *out_packet, uint32_t timeout_ms) {
    if (out_packet == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = lidar_find_header();
    if (err != ESP_OK) {
        return err;
    }

    // Header + VerLen already consumed by lidar_find_header; read the rest.
    uint8_t buf[LIDAR_PACKET_LEN];
    buf[0] = LIDAR_HEADER;
    buf[1] = LIDAR_VERLEN;

    err = uart_bus_read_exact(&lidar_bus, &buf[2], LIDAR_PACKET_LEN - 2, timeout_ms);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t expected_crc = buf[LIDAR_PACKET_LEN - 1];
    uint8_t computed_crc = crc8_calc(buf, LIDAR_CRC_COVERED_LEN);
    if (computed_crc != expected_crc) {
        //log_msg_lvl(ESP_LOG_ERROR, TAG, "CRC mismatch (expected 0x%02X, got 0x%02X) - dropping packet", 
        //    expected_crc, computed_crc);
        return ESP_ERR_INVALID_CRC;
    }

    out_packet->speed_deg_s          = (uint16_t)(buf[2] | (buf[3] << 8));
    out_packet->start_angle_centideg = (uint16_t)(buf[4] | (buf[5] << 8));

    for (int i = 0; i < LIDAR_POINTS_PER_PACKET; i++) {
        size_t off = 6 + (i * 3);
        out_packet->points[i].distance_mm = (uint16_t)(buf[off] | (buf[off + 1] << 8));
        out_packet->points[i].intensity   = buf[off + 2];
    }

    out_packet->end_angle_centideg = (uint16_t)(buf[42] | (buf[43] << 8));
    out_packet->timestamp_ms       = (uint16_t)(buf[44] | (buf[45] << 8));

    return ESP_OK;
}

static void lidar_task(void *params) {
    (void)params;
    esp_err_t err;

    err = uart_bus_init(&lidar_bus, LIDAR_UART_PORT,
        UART_PIN_NO_CHANGE /* no TX, one-way device */,
        LIDAR_RX_GPIO, LIDAR_BAUD_RATE, LIDAR_RX_BUF_SIZE);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error initializing LD500: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    log_msg(TAG, "LD500 initialized RX %d", LIDAR_RX_GPIO);

    lidar_packet_t pkt;
    lidar_packet_t batch[LIDAR_BATCH_SIZE];
    int batch_count = 0;

    while (true) {
        err = lidar_read_packet(&pkt, 200);
        if (err == ESP_OK) {
            batch[batch_count] = pkt;
            batch_count++;
            if (batch_count == LIDAR_BATCH_SIZE) {
                //log_msg(TAG, "Sending batch lidar (%d)", batch_count);
#if CONFIG_USE_UDPLIB
                header_sensor_t header = {0};
                header.esp_id = (uint8_t)CONFIG_ESP_ID;
                header.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
                header.type = SENSOR_TYPE_LIDAR;

                uint8_t buf[HEADER_SENSOR_SIZE + LIDAR_BATCH_SIZE * LIDAR_PACKET_SERIALIZED_SIZE];
                serialize_header(&header, buf);
                for (int i = 0; i < LIDAR_BATCH_SIZE; i++) {
                    serialize_lidar_packet(&batch[i], &buf[HEADER_SENSOR_SIZE + i * LIDAR_PACKET_SERIALIZED_SIZE]);
                }

                send_udp_sensor(buf, sizeof(buf));
#endif
                batch_count = 0;
            }
        } else {
            //log_msg_lvl(ESP_LOG_ERROR, TAG, "Error reading lidar packet (%s)", esp_err_to_name(err));
        }
    }
}

esp_err_t init_lidar(void) {
    return xTaskCreate(lidar_task, "lidar_task", 4096, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

#else // !CONFIG_USE_LIDAR

esp_err_t init_lidar(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_LIDAR