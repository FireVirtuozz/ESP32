#ifndef UDPLIB_H_
#define UDPLIB_H_

#include <inttypes.h>
#include <esp_err.h>

// Initialize UDP server
void udp_server_init();

// Initialize UDP client connection to Host
void udp_client_init();

typedef struct udp_msg_st {
    uint8_t data[255];
    uint8_t len;
} udp_msg_t;

typedef struct header_udp_frame_st {
    uint8_t type;
    uint8_t flags;
    uint32_t timestamp;
} header_udp_frame_t;

// Send a UDP message
void send_udp_msg(udp_msg_t *msg);

#endif