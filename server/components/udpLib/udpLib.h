#ifndef UDPLIB_H_
#define UDPLIB_H_

#define UDP_MSG_SIZE 250
#define UDP_MAX_SIZE 1400

#include <inttypes.h>
#include <esp_err.h>

// Initialize UDP server
void udp_server_init();

// Initialize UDP client connection to Host
void udp_client_init();

// Send a UDP log message
void send_udp_log(const uint8_t * data, uint32_t len);

// Send a UDP message
void send_udp_sensor(const uint8_t * data, uint32_t len);

// Send a JPEG UDP message
void send_udp_jpeg(const uint8_t * data, uint32_t len);

// Send a dump UDP message
void send_udp_dump(const uint8_t * data, uint32_t len);

int get_command_packet_received();

#endif