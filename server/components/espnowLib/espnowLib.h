#ifndef ESPNOWLIB_H_
#define ESPNOWLIB_H_

#define ESPNOW_MSG_SIZE 250

#include <inttypes.h>

typedef struct espnow_msg_st {
    uint8_t data[ESPNOW_MSG_SIZE];
    uint8_t len;
} espnow_msg_t;

typedef struct header_espnow_frame_st {
    uint8_t type;
    uint8_t flags;
    uint32_t timestamp;
} header_espnow_frame_t;

void espnow_init(void);

void send_espnow_msg(espnow_msg_t *msg);

#endif