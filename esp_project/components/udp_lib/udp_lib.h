#ifndef UDP_LIB_H_
#define UDP_LIB_H_

#define UDP_MSG_SIZE 250
#define UDP_MAX_SIZE 1400

#include <inttypes.h>
#include <esp_err.h>
#include <stdatomic.h>

extern _Atomic uint32_t nb_packets_received;

// Initialize UDP server
void udp_server_init();

// Initialize UDP client connection to Host
void udp_client_init();

// Send a UDP log message
void send_udp_log(const uint8_t * data, uint32_t len);

// Send a UDP message
void send_udp_sensor(const uint8_t * data, uint32_t len);

// Send a config message
void send_udp_cfg(const uint8_t * data, uint32_t len);

// Send a JPEG UDP message
void send_udp_jpeg(const uint8_t * data, uint32_t len);

// Send a dump UDP message
void send_udp_dump(const uint8_t * data, uint32_t len);

// Reset IP hello ack (disconnected)
void reset_ip_hello();

#endif