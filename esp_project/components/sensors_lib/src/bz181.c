#include "bz181.h"
#include "peripherals/uart_helper.h"
#include "log_lib.h"
#include "sensors_lib.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "gps_driver";

#if CONFIG_USE_GPS

#include "udp_lib.h"
#include "esp_timer.h"

#define GPS_HEADER 0x24 // Caractère '$'
#define GPS_MAX_SENTENCE_LEN 128
#define GPS_UART_PORT   UART_NUM_2
#define GPS_BAUD_RATE   115200
#define GPS_RX_BUF_SIZE   2048

#define MAX_SATELLITES_VIEW 48
#define MAX_ACTIVE_SATS 16

// Taille d'un satellite sérialisé :
// 2 octets (azimuth: uint16_t) + 4 octets (prn, elevation, snr, constellation: uint8_t/int8_t)
#define GPS_SATELLITE_SERIALIZED_SIZE (sizeof(uint16_t) + 4 * sizeof(uint8_t))
// = 2 + 4 = 6 octets

#define GPS_FULL_STATE_SERIALIZED_SIZE \
    (sizeof(((gps_full_state_t*)0)->utc_time) \
     + sizeof(((gps_full_state_t*)0)->utc_date) \
     + sizeof(uint8_t)  /* status */ \
     + sizeof(uint8_t)  /* mode_indicator */ \
     + sizeof(double)   /* latitude */ \
     + sizeof(double)   /* longitude */ \
     + sizeof(float)    /* speed_knots */ \
     + sizeof(float)    /* speed_kmh */ \
     + sizeof(float)    /* course_over_ground */ \
     + sizeof(uint8_t)  /* fix_quality */ \
     + sizeof(uint8_t)  /* satellites_used */ \
     + sizeof(float)    /* hdop */ \
     + sizeof(float)    /* altitude_m */ \
     + sizeof(float)    /* geoid_height_m */ \
     + sizeof(char)     /* fix_mode */ \
     + sizeof(uint8_t)  /* fix_type */ \
     + sizeof(float)    /* pdop */ \
     + sizeof(float)    /* vdop */ \
     + MAX_ACTIVE_SATS * sizeof(uint8_t) /* active_sats_ids */ \
     + sizeof(uint8_t)  /* active_sats_count */ \
     + MAX_SATELLITES_VIEW * GPS_SATELLITE_SERIALIZED_SIZE /* sats_in_view */ \
     + sizeof(uint16_t) /* total_sats_in_view */ \
     + sizeof(uint32_t) /* last_rmc_ms */ \
     + sizeof(uint32_t) /* last_gga_ms */ \
     + sizeof(uint32_t) /* last_gsa_ms */ \
     + sizeof(uint32_t) /* last_gsv_ms */)

typedef enum {
    GNSS_CONST_UNKNOWN = 0,
    GNSS_CONST_GPS,
    GNSS_CONST_GLONASS,
    GNSS_CONST_GALILEO,
    GNSS_CONST_BEIDOU,
    GNSS_CONST_QZSS
} gnss_constellation_t;

typedef struct {
    uint16_t azimuth;       // 0-359° (2 octets)
    uint8_t prn;            // 1 octet
    int8_t elevation;       // 0-90° (1 octet)
    int8_t snr;             // 0-99 dBHz (1 octet)
    uint8_t constellation;  // 1 octet
} gps_satellite_t; // Total : 6 octets (au lieu de 17)

typedef struct {
    // --- RMC ---
    char utc_time[12];
    char utc_date[8];
    char status;
    char mode_indicator;
    double latitude;
    double longitude;
    float speed_knots;
    float speed_kmh;
    float course_over_ground;

    // --- GGA ---
    uint8_t fix_quality;
    uint8_t satellites_used;
    float hdop;
    float altitude_m;
    float geoid_height_m;

    // --- GSA ---
    char fix_mode;
    uint8_t fix_type;
    float pdop;
    float vdop;
    uint8_t active_sats_ids[MAX_ACTIVE_SATS]; // uint8_t au lieu de int
    uint8_t active_sats_count;

    // --- GSV ---
    gps_satellite_t sats_in_view[MAX_SATELLITES_VIEW]; // 6 octets par sat !
    uint16_t total_sats_in_view;

    // --- Métadonnées ---
    uint32_t last_rmc_ms;
    uint32_t last_gga_ms;
    uint32_t last_gsa_ms;
    uint32_t last_gsv_ms;
} gps_full_state_t;

static void serialize_gps_state(const gps_full_state_t *state, uint8_t *buf) {
    uint16_t len = 0;

    memcpy(&buf[len], state->utc_time, sizeof(state->utc_time));
    len += sizeof(state->utc_time);

    memcpy(&buf[len], state->utc_date, sizeof(state->utc_date));
    len += sizeof(state->utc_date);

    buf[len++] = (uint8_t)state->status;
    buf[len++] = (uint8_t)state->mode_indicator;

    memcpy(&buf[len], &state->latitude, sizeof(double));
    len += sizeof(double);

    memcpy(&buf[len], &state->longitude, sizeof(double));
    len += sizeof(double);

    memcpy(&buf[len], &state->speed_knots, sizeof(float));
    len += sizeof(float);

    memcpy(&buf[len], &state->speed_kmh, sizeof(float));
    len += sizeof(float);

    memcpy(&buf[len], &state->course_over_ground, sizeof(float));
    len += sizeof(float);

    buf[len++] = state->fix_quality;
    buf[len++] = state->satellites_used;

    memcpy(&buf[len], &state->hdop, sizeof(float));
    len += sizeof(float);

    memcpy(&buf[len], &state->altitude_m, sizeof(float));
    len += sizeof(float);

    memcpy(&buf[len], &state->geoid_height_m, sizeof(float));
    len += sizeof(float);

    buf[len++] = (uint8_t)state->fix_mode;
    buf[len++] = state->fix_type;

    memcpy(&buf[len], &state->pdop, sizeof(float));
    len += sizeof(float);

    memcpy(&buf[len], &state->vdop, sizeof(float));
    len += sizeof(float);

    for (int i = 0; i < MAX_ACTIVE_SATS; i++) {
        buf[len++] = state->active_sats_ids[i];
    }

    buf[len++] = state->active_sats_count;

    for (int i = 0; i < MAX_SATELLITES_VIEW; i++) {
        const gps_satellite_t *sat = &state->sats_in_view[i];

        // azimuth (uint16_t)
        memcpy(&buf[len], &sat->azimuth, sizeof(uint16_t));
        len += sizeof(uint16_t);

        // prn, elevation, snr, constellation (1 octet chacun)
        buf[len++] = sat->prn;
        buf[len++] = (uint8_t)sat->elevation;
        buf[len++] = (uint8_t)sat->snr;
        buf[len++] = (uint8_t)sat->constellation;
    }

    // total_sats_in_view (uint16_t)
    memcpy(&buf[len], &state->total_sats_in_view, sizeof(uint16_t));
    len += sizeof(uint16_t);

    memcpy(&buf[len], &state->last_rmc_ms, sizeof(uint32_t));
    len += sizeof(uint32_t);

    memcpy(&buf[len], &state->last_gga_ms, sizeof(uint32_t));
    len += sizeof(uint32_t);

    memcpy(&buf[len], &state->last_gsa_ms, sizeof(uint32_t));
    len += sizeof(uint32_t);

    memcpy(&buf[len], &state->last_gsv_ms, sizeof(uint32_t));
    len += sizeof(uint32_t);
}

static void print_gps_state(const gps_full_state_t *state) {
    log_msg(TAG, "==================== GPS FULL STATE ====================");
    log_msg(TAG, "[RMC] Time: %s | Date: %s | Status: %c | Mode: %c", state->utc_time, state->utc_date, state->status, state->mode_indicator);
    log_msg(TAG, "[POS] Lat: %.6f | Lon: %.6f | Spd: %.1f km/h | Course: %.1f°", state->latitude, state->longitude, state->speed_kmh, state->course_over_ground);
    log_msg(TAG, "[GGA] Fix Quality: %d | Sats Used: %d | Alt: %.1f m | HDOP: %.2f | Geoid: %.1f m", state->fix_quality, state->satellites_used, state->altitude_m, state->hdop, state->geoid_height_m);

    char active_buf[96] = {0};
    int offset = 0;
    for (int i = 0; i < state->active_sats_count && i < MAX_ACTIVE_SATS; i++) {
        offset += snprintf(active_buf + offset, sizeof(active_buf) - offset, "%d ", state->active_sats_ids[i]);
    }
    log_msg(TAG, "[GSA] Type: %d (Mode: %c) | PDOP: %.2f | VDOP: %.2f | Active (%d): [%s]", state->fix_type, state->fix_mode, state->pdop, state->vdop, state->active_sats_count, active_buf);

    log_msg(TAG, "[GSV] Total Sats in View: %d", state->total_sats_in_view);
    for (int i = 0; i < state->total_sats_in_view && i < MAX_SATELLITES_VIEW; i++) {
        const gps_satellite_t *sat = &state->sats_in_view[i];
        log_msg(TAG, "      -> Sat PRN: %02d | Const: %d | El: %2d° | Az: %3d° | SNR: %2d dBHz",
                 sat->prn, sat->constellation, sat->elevation, sat->azimuth, sat->snr);
    }
    log_msg(TAG, "========================================================");
}

static bool get_nmea_field(const char *sentence, int field_index, char *dest, int max_len) {
    int current_field = 0;
    const char *p = sentence;
    while (*p && *p != '*') {
        if (*p == ',') {
            current_field++;
            p++;
            continue;
        }
        if (current_field == field_index) {
            int i = 0;
            while (*p && *p != ',' && *p != '*' && i < max_len - 1) {
                dest[i++] = *p++;
            }
            dest[i] = '\0';
            return true;
        }
        p++;
    }
    dest[0] = '\0';
    return false;
}

static double nmea_to_decimal(const char *coord, char dir) {
    if (!coord || coord[0] == '\0') return 0.0;
    double raw = atof(coord);
    int deg = (int)(raw / 100);
    double min = raw - (deg * 100);
    double dec = deg + (min / 60.0);
    if (dir == 'S' || dir == 'W') dec = -dec;
    return dec;
}

// Copie sûre, null-terminée, bornée à la taille réelle du buffer destination.
// Remplace les strncpy(dest, buf, sizeof(dest)) qui ne garantissaient pas
// la terminaison si la source faisait exactement sizeof(dest) caractères.
static void safe_field_copy(char *dest, size_t dest_size, const char *src) {
    if (dest_size == 0) return;
    size_t n = strlen(src);
    if (n >= dest_size) n = dest_size - 1;
    memcpy(dest, src, n);
    dest[n] = '\0';
}

static bool parse_nmea_giga(const char *sentence, gps_full_state_t *state, uint32_t now_ms) {
    char header[16];
    if (!get_nmea_field(sentence, 0, header, sizeof(header))) return false;

    // --- RMC ---
    if (strncmp(header, "$GNRMC", 6) == 0 || strncmp(header, "$GPRMC", 6) == 0) {
        char buf[32];

        if (get_nmea_field(sentence, 1, buf, sizeof(buf))) {
            safe_field_copy(state->utc_time, sizeof(state->utc_time), buf);
        }
        if (get_nmea_field(sentence, 2, buf, sizeof(buf)) && buf[0] != '\0') {
            state->status = buf[0];
        }

        char lat[32], ns[8];
        if (get_nmea_field(sentence, 3, lat, sizeof(lat)) && get_nmea_field(sentence, 4, ns, sizeof(ns)) && ns[0] != '\0') {
            state->latitude = nmea_to_decimal(lat, ns[0]);
        }

        char lon[32], ew[8];
        if (get_nmea_field(sentence, 5, lon, sizeof(lon)) && get_nmea_field(sentence, 6, ew, sizeof(ew)) && ew[0] != '\0') {
            state->longitude = nmea_to_decimal(lon, ew[0]);
        }

        if (get_nmea_field(sentence, 7, buf, sizeof(buf)) && buf[0] != '\0') {
            state->speed_knots = atof(buf);
            state->speed_kmh = state->speed_knots * 1.852f;
        }
        if (get_nmea_field(sentence, 8, buf, sizeof(buf)) && buf[0] != '\0') {
            state->course_over_ground = atof(buf);
        }
        if (get_nmea_field(sentence, 9, buf, sizeof(buf))) {
            safe_field_copy(state->utc_date, sizeof(state->utc_date), buf);
        }

        // Mode indicator (champ 12, index 11) : absent sur NMEA < 2.3,
        // donc le champ peut légitimement être vide - ne pas écraser
        // l'ancienne valeur dans ce cas plutôt que la remettre à '\0'.
        if (get_nmea_field(sentence, 11, buf, sizeof(buf)) && buf[0] != '\0') {
            state->mode_indicator = buf[0];
        }

        state->last_rmc_ms = now_ms;
        return true;
    }
    // --- GGA ---
    else if (strncmp(header, "$GNGGA", 6) == 0 || strncmp(header, "$GPGGA", 6) == 0) {
        char buf[32];
        if (get_nmea_field(sentence, 6, buf, sizeof(buf)) && buf[0] != '\0') state->fix_quality = atoi(buf);
        if (get_nmea_field(sentence, 7, buf, sizeof(buf)) && buf[0] != '\0') state->satellites_used = atoi(buf);
        if (get_nmea_field(sentence, 8, buf, sizeof(buf)) && buf[0] != '\0') state->hdop = atof(buf);
        if (get_nmea_field(sentence, 9, buf, sizeof(buf)) && buf[0] != '\0') state->altitude_m = atof(buf);
        if (get_nmea_field(sentence, 11, buf, sizeof(buf)) && buf[0] != '\0') state->geoid_height_m = atof(buf);

        state->last_gga_ms = now_ms;
        return true;
    }
    // --- GSA ---
    else if (strstr(header, "GSA") != NULL) {
        char buf[32];
        if (get_nmea_field(sentence, 1, buf, sizeof(buf)) && buf[0] != '\0') state->fix_mode = buf[0];
        if (get_nmea_field(sentence, 2, buf, sizeof(buf)) && buf[0] != '\0') state->fix_type = atoi(buf);

        state->active_sats_count = 0;
        for (int i = 0; i < 12; i++) {
            if (get_nmea_field(sentence, 3 + i, buf, sizeof(buf)) && buf[0] != '\0') {
                int id = atoi(buf);
                if (id > 0 && state->active_sats_count < MAX_ACTIVE_SATS) {
                    state->active_sats_ids[state->active_sats_count++] = id;
                }
            }
        }
        if (get_nmea_field(sentence, 15, buf, sizeof(buf)) && buf[0] != '\0') state->pdop = atof(buf);
        if (get_nmea_field(sentence, 16, buf, sizeof(buf)) && buf[0] != '\0') {
            float h = atof(buf);
            if (h > 0) state->hdop = h;
        }
        if (get_nmea_field(sentence, 17, buf, sizeof(buf)) && buf[0] != '\0') state->vdop = atof(buf);

        state->last_gsa_ms = now_ms;
        return true;
    }
    // --- GSV ---
    else if (strstr(header, "GSV") != NULL) {
        // header[2] ne distingue pas $GNGSV (générique multi-constellation,
        // très courant sur u-blox M10 type BZ181) des headers mono-constellation
        // $GPGSV/$GLGSV/etc. Sur $GNGSV la constellation réelle d'un satellite
        // donné n'est de toute façon pas portée par le header de la trame -
        // elle se déduit du numéro de PRN selon la norme NMEA (plages par
        // constellation), pas d'une lecture de caractère ici. On ne devine
        // plus au hasard: on marque UNKNOWN sur $GNGSV plutôt que de mal
        // classer silencieusement, et on classe correctement les headers
        // mono-constellation explicites.
        gnss_constellation_t constel = GNSS_CONST_UNKNOWN;
        if (strncmp(header, "$GP", 3) == 0) constel = GNSS_CONST_GPS;
        else if (strncmp(header, "$GL", 3) == 0) constel = GNSS_CONST_GLONASS;
        else if (strncmp(header, "$GA", 3) == 0) constel = GNSS_CONST_GALILEO;
        else if (strncmp(header, "$GB", 3) == 0) constel = GNSS_CONST_BEIDOU;
        else if (strncmp(header, "$GQ", 3) == 0) constel = GNSS_CONST_QZSS;
        // $GN... reste UNKNOWN ici volontairement (voir commentaire ci-dessus)

        char buf[32];
        if (get_nmea_field(sentence, 3, buf, sizeof(buf)) && buf[0] != '\0') {
            state->total_sats_in_view = atoi(buf);
        }

        int msg_idx = 1;
        if (get_nmea_field(sentence, 2, buf, sizeof(buf)) && buf[0] != '\0') {
            msg_idx = atoi(buf);
        }
        int base_idx = (msg_idx - 1) * 4;

        for (int i = 0; i < 4; i++) {
            int field_offset = 4 + (i * 4);
            char prn_str[8], el_str[8], az_str[8], snr_str[8];

            get_nmea_field(sentence, field_offset + 0, prn_str, sizeof(prn_str));
            if (prn_str[0] == '\0') break;

            get_nmea_field(sentence, field_offset + 1, el_str, sizeof(el_str));
            get_nmea_field(sentence, field_offset + 2, az_str, sizeof(az_str));
            get_nmea_field(sentence, field_offset + 3, snr_str, sizeof(snr_str));

            int target_idx = base_idx + i;
            if (target_idx < MAX_SATELLITES_VIEW) {
                state->sats_in_view[target_idx].prn = atoi(prn_str);
                state->sats_in_view[target_idx].elevation = (el_str[0] != '\0') ? atoi(el_str) : 0;
                state->sats_in_view[target_idx].azimuth = (az_str[0] != '\0') ? atoi(az_str) : 0;
                state->sats_in_view[target_idx].snr = (snr_str[0] != '\0') ? atoi(snr_str) : 0;
                state->sats_in_view[target_idx].constellation = constel;
            }
        }

        state->last_gsv_ms = now_ms;
        return true;
    }

    return false;
}

static uart_bus_t gps_bus = {0};

static esp_err_t gps_find_header(void) {
    uint8_t byte;
    for (int attempts = 0; attempts < GPS_MAX_SENTENCE_LEN * 2; attempts++) {
        int n = uart_bus_read(&gps_bus, &byte, 1, 100);
        if (n <= 0) {
            return ESP_ERR_TIMEOUT;
        }
        if (byte == GPS_HEADER) {
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t gps_read_sentence(char *out_buf, size_t max_len, uint32_t timeout_ms) {
    if (out_buf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = gps_find_header();
    if (err != ESP_OK) {
        return err;
    }

    out_buf[0] = '$';
    size_t idx = 1;

    while (idx < max_len - 1) {
        uint8_t byte;
        int n = uart_bus_read(&gps_bus, &byte, 1, timeout_ms);
        if (n <= 0) {
            return ESP_ERR_TIMEOUT;
        }

        out_buf[idx++] = (char)byte;
        if (byte == '\n') {
            break;
        }
    }

    out_buf[idx] = '\0';
    return ESP_OK;
}

static uint8_t gps_udp_buf[HEADER_SENSOR_SIZE + GPS_FULL_STATE_SERIALIZED_SIZE];

static void gps_task(void *params) {
    (void)params;

    if (uart_bus_init(&gps_bus, GPS_UART_PORT,
        UART_PIN_NO_CHANGE,
        GPS_RX_GPIO, GPS_BAUD_RATE, GPS_RX_BUF_SIZE) != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    log_msg(TAG, "BZ-181 GPS initialized RX %d @ %d baud", GPS_RX_GPIO, GPS_BAUD_RATE);

    gps_full_state_t gps_state = {0};
    char sentence[GPS_MAX_SENTENCE_LEN];

    uint32_t last_udp_send_ms = 0;
    const uint32_t UDP_SEND_INTERVAL_MS = 1000;

    while (true) {
        esp_err_t err = gps_read_sentence(sentence, sizeof(sentence), 200);
        if (err == ESP_OK) {
            sentence[strcspn(sentence, "\r\n")] = '\0';

            //log_msg(TAG, "%s", sentence);

            uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
            parse_nmea_giga(sentence, &gps_state, now_ms);

            if (now_ms - last_udp_send_ms >= UDP_SEND_INTERVAL_MS) {
                //print_gps_state(&gps_state);
#if CONFIG_USE_UDPLIB
                header_sensor_t header = {0};
                header.esp_id = (uint8_t)CONFIG_ESP_ID;
                header.timestamp = now_ms;
                header.type = SENSOR_TYPE_GPS;

                serialize_header(&header, gps_udp_buf);
                serialize_gps_state(&gps_state, &gps_udp_buf[HEADER_SENSOR_SIZE]);
                send_udp_sensor(gps_udp_buf, sizeof(gps_udp_buf));
#endif
                last_udp_send_ms = now_ms;
            }
        } else {
            log_msg_lvl(ESP_LOG_ERROR, TAG, "Error reading GPS sentence (%s)", esp_err_to_name(err));
        }
    }
}

esp_err_t init_gps(void) {
    return xTaskCreate(gps_task, "gps_task", 8192, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

#else // !CONFIG_USE_GPS

esp_err_t init_gps(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_GPS