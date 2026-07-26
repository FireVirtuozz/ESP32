#ifndef ESPNOWLIB_H_
#define ESPNOWLIB_H_

#define ESPNOW_MSG_SIZE 250

#include <inttypes.h>

typedef enum espnow_virtual_port_e {
    DUMP = 0,
    LOGS = 1,
    SENSORS = 2,
    VIDEO = 3,
    CMD = 4,
} espnow_virtual_port;

typedef struct espnow_msg_st {
    uint8_t *data;
    uint8_t len;
} espnow_msg_t;

#define HEADER_ESPNOW_SIZE (3 * sizeof(uint8_t))

typedef struct header_espnow_frame_st {
    uint8_t flags; //[0] = udp dispatch, 
    espnow_virtual_port packet_type;
    uint8_t needs_frag;
} header_espnow_frame_t;


void espnow_init(void);

void send_espnow_msg(header_espnow_frame_t *hd, const uint8_t * data, uint32_t len);

#endif