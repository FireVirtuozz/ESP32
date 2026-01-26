#include "udpLib.h"
#include <esp_log.h>
#include <string.h>
#include <stdlib.h>
#include "ledLib.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "esp_timer.h"

#define PORT 3333

/**
 * How it works:
 * 
 * handler for server : initialize it with default config, port 80 and register uri handlers
 * 
 * uri handlers : declare them with a struct and a function associated
 * 
 * function handler : method type, get frame length first then payload, give instructions by payload
 * and return response to client
 * 
 * function send text : initialize frame and to send all clients, use send frame with server handler as request
 */

static const char *TAG = "udp_library"; // tag of this library

static void udp_server_task(void *pvParameters)
{
    int8_t temp_buffer[8];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    struct sockaddr_in6 dest_addr;

    static int nb_packets_received = 0;

    while (1) {

        int64_t last_packet_time = 0;
        int64_t sum_intervals = 0;
        int packet_count = 0;

        //if ipv4
        if (addr_family == AF_INET) {
            struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr; //ipv4 struct
            dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY); //setting ip type receiving
            dest_addr_ip4->sin_family = AF_INET; //setting type ipv4
            dest_addr_ip4->sin_port = htons(PORT); //setting port
            ip_protocol = IPPROTO_IP; //setting ip protocol
        }

        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol); //create socket
        if (sock < 0) {
            log_mqtt(LOG_ERROR, TAG, true, "Unable to create socket: errno %d", errno);
            break;
        }
        log_mqtt(LOG_INFO, TAG, true, "Socket created");

        // Set timeout : 10 seconds
        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout); //socket option timeout

        //bind socket to port
        int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            log_mqtt(LOG_ERROR, TAG, true, "Socket unable to bind: errno %d", errno);
        }
        log_mqtt(LOG_INFO, TAG, true, "Socket bound, port %d", PORT);

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t socklen = sizeof(source_addr);

        while (1) {
            log_mqtt(LOG_DEBUG, TAG, false, "Waiting for data");

            //wait to receive data, store source socket addr
            int len = recvfrom(sock, temp_buffer, sizeof(temp_buffer), 0, (struct sockaddr *)&source_addr, &socklen);

            // Error occurred during receiving
            if (len < 0) {
                log_mqtt(LOG_ERROR, TAG, true, "recvfrom failed: errno %d", errno);
                break;
            } else { // Data received

                int64_t current_time = esp_timer_get_time(); // ms
                if (last_packet_time != 0) {
                    int64_t delta = current_time - last_packet_time; // delta us
                    sum_intervals += delta;
                    packet_count++;

                    int64_t average_interval = sum_intervals / packet_count; // average in us
                    log_mqtt(LOG_INFO, TAG, false, "Interval: %lld us, Average: %lld us", delta, average_interval);
                }
                last_packet_time = current_time;

                nb_packets_received++;

                uint8_t type = temp_buffer[0];

                log_mqtt(LOG_DEBUG, TAG, false, "Received %d bytes: packet number : %d",
                    len, nb_packets_received);

                switch (type) {
                    case 0: //gamepad type control
                        log_mqtt(LOG_DEBUG, TAG, false, "Gamepad axes raw: [%d,%d,%d,%d,%d,%d]", 
                            temp_buffer[1], temp_buffer[2], temp_buffer[3],
                            temp_buffer[4], temp_buffer[5], temp_buffer[6]);
                        log_mqtt(LOG_DEBUG, TAG, false, "Gamepad button raw: [%d,%d,%d,%d,%d,%d,%d,%d]", 
                            (temp_buffer[7] & 0x01) ? 1 : 0, (temp_buffer[7] & 0x02) ? 1 : 0,
                            (temp_buffer[7] & 0x04) ? 1 : 0, (temp_buffer[7] & 0x08) ? 1 : 0,
                            (temp_buffer[7] & 0x10) ? 1 : 0, (temp_buffer[7] & 0x20) ? 1 : 0,
                            (temp_buffer[7] & 0x40) ? 1 : 0, (temp_buffer[7] & 0x80) ? 1 : 0);

                        ledc_angle((int16_t)((temp_buffer[1] + 100) * 9 / 10)); //left_x
            
                        if (temp_buffer[6] > -95) {
                            ledc_motor((int16_t)((temp_buffer[6] + 100) / 2)); //right_trigger
                        } else if (temp_buffer[5] > -95) {
                            ledc_motor((int16_t)((temp_buffer[5] + 100) / (-2))); //left_trigger
                        } else {
                            ledc_motor(0);
                        }
                        break;
                    
                    case 1: //android type control
                        log_mqtt(LOG_DEBUG, TAG, false, "Android axes raw: [%d,%d]", 
                            temp_buffer[1], temp_buffer[2]);
                        
                        ledc_angle((int16_t)((temp_buffer[2] + 100) * 9 / 10)); //direction
                        ledc_motor((int16_t)(temp_buffer[1])); //accel
                        
                        break;
                    
                    default:
                        break;
                }
            }
                
            }

            //shutdown socket if error
            if (sock != -1) {
                log_mqtt(LOG_ERROR, TAG, true, "Shutting down socket and restarting...");
                shutdown(sock, 0);
                close(sock);
            }
        }
    vTaskDelete(NULL);
}

/**
 * Function to init server : 
 * Init server handler with default config on port 80 and register uri handlers
 */
void udp_server_init()
{
    xTaskCreatePinnedToCore(udp_server_task, "udp_server", 8192, (void*)AF_INET, 15, NULL, 1);
}