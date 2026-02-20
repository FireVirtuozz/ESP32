#include "udpLib.h"
#include <esp_log.h>
#include <string.h>
#include <stdlib.h>
#include "ledLib.h"
#include "mqttLib.h"
#include "cmdLib.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#define TIME_STATS 0 //for time statistics
#if TIME_STATS
#include "esp_timer.h"
#endif

#define PORT 3333

#define UDP_COMMANDS_TIMEOUT 900000 //us, no more than 1s = 1000000

//TODO : IPv6 support, length commands buffer check, security

#define PACKET_DEBUG_ACTIVATED 0 //for packet debug
#define INFO_LOGS 0 //for info logs
#define ADDRESS_DEBUG 0 //for address debug

static const char *TAG = "udp_library"; // tag of this library

#if PACKET_DEBUG_ACTIVATED
static char * sock_type_to_str(int val) {
    switch (val)
    {
    case SOCK_STREAM:
        return "stream (tcp)";
    case SOCK_DGRAM:
        return "dgram (udp)";
    case SOCK_RAW:
        return "raw (ip)";
    
    default:
        return "unknown socket type";
    }
}

static char * cmsg_protocol_spe_to_str(int val) {
    switch (val)
    {
    //Packet info IPV4 only
    case IP_PKTINFO:
        return "Packet info";
    //Time to Live
    case IP_TTL:
        return "TTL";
    //type of service (QoS)
    case IP_TOS:
        return "TOS";
    
    default:
        return "unknown special protocol";
    }
}

#endif

#if ADDRESS_DEBUG || PACKET_DEBUG_ACTIVATED
static char * cmsg_protocol_to_str(int val) {
    switch (val)
    {
    //IPV4 packet
    case IPPROTO_IP:
        return "IP normal";
    //ICMP packet (ping, traceroute..)
    case IPPROTO_ICMP:
        return "ICMP";
    //TCP Header options
    case IPPROTO_TCP:
        return "TCP";
    //UDP Lite
    case IPPROTO_UDP:
        return "UDP";
    
    default:
        return "unknown protocol";
    }
}
#endif

#if ADDRESS_DEBUG
static char * addr_family_to_str(int val) {
    switch (val)
    {
    case AF_UNSPEC:
        return "Not specified";
    case AF_INET6:
        return "IPV6";
    case AF_INET:
        return "IPV4";
    
    default:
        return "unknown";
    }
}

static char * addr_family_pf_to_str(int val) {
    switch (val)
    {
    case PF_UNSPEC:
        return "Not specified";
    case PF_INET6:
        return "IPV6";
    case PF_INET:
        return "IPV4";
    
    default:
        return "unknown";
    }
}

static char * source_ip_to_str(int val) {
    switch (val)
    {
    case INADDR_NONE: //same as broadcast
        return "None (255.255.255.255)";
    case INADDR_LOOPBACK:
        return "Local (127.0.0.1)";
    case INADDR_ANY:
        return "Any (0.0.0.0)";
    
    default:
        return "unknown";
    }
}
#endif

static void udp_server_task(void *pvParameters)
{
    int8_t temp_buffer[10];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    struct sockaddr_in6 dest_addr;

#if INFO_LOGS
    static int nb_packets_received = 0;
#endif

    while (1) {

#if TIME_STATS
        int64_t last_packet_time = 0;
        int64_t sum_intervals = 0;
        int packet_count = 0;
#endif
#if ADDRESS_DEBUG
        log_mqtt(LOG_INFO, TAG, true, "Address family : %s", addr_family_to_str(addr_family));
#endif

        //For multi-byte numbers : ENDIANNESS, order of sending bytes for a number
        //ESP32 : Little Endian; Low bytes first
        //Internet : Big Endian; High bytes first

        //Example : 
        //Internet [Big] port 80 0x0050 memory : [0] = 00, [1] = 50
        //ESP32 [Little] port 80 0x0050 memory : [0] = 50, [1] = 00

        //swapping Little Endian <-> Big Endian
        //htons [short](Port), htonl [long](IP) : host CPU -> Network
        //ntohs, ntohl : Network -> host CPU

        //if ipv4
        if (addr_family == AF_INET) {
            struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr; //ipv4 struct
            dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY); //setting ip type receiving
            dest_addr_ip4->sin_family = AF_INET; //setting type ipv4
            dest_addr_ip4->sin_port = htons(PORT); //setting port
            ip_protocol = IPPROTO_IP; //setting ip protocol
#if ADDRESS_DEBUG
            log_mqtt(LOG_INFO, TAG, true, "IP protocol : %s", cmsg_protocol_to_str(ip_protocol));
            log_mqtt(LOG_INFO, TAG, true, "IP : %s", source_ip_to_str(ntohl(dest_addr_ip4->sin_addr.s_addr)));
            log_mqtt(LOG_INFO, TAG, true, "Family : %s", addr_family_to_str(dest_addr_ip4->sin_family));
            log_mqtt(LOG_INFO, TAG, true, "Port : %d", ntohs(dest_addr_ip4->sin_port));
#endif
        }


        //create socket 
        //inline : implemented in lwip's socket header for better performance
        //addr family : ipv4
        //sock_dgram : udp
        //ip protocol : auto
        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol); //create socket    
        if (sock < 0) {
            log_mqtt(LOG_ERROR, TAG, true, "Unable to create socket: %d, %s", errno, strerror(errno));
            break;
        }
        log_mqtt(LOG_INFO, TAG, true, "Socket created");

#if PACKET_DEBUG_ACTIVATED
        //set receive packt info for debug (ipv4 only)
        int enable = 1;
        lwip_setsockopt(sock, IPPROTO_IP, IP_PKTINFO, &enable, sizeof(enable));
#endif

        // Set timeout : 10 seconds
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = UDP_COMMANDS_TIMEOUT;
        setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout); //socket option timeout

#if PACKET_DEBUG_ACTIVATED
        int val;
        socklen_t len = sizeof(val);
        //reuse port if already used in TIME_WAIT (state TCP when connection closed)
        if (getsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &val, &len) == 0) {
            log_mqtt(LOG_INFO, TAG, true, "Reuse address : %d", val);
        } else {
            log_mqtt(LOG_ERROR, TAG, true, "Error getting reuse address option socket info");
        }
        //in TCP : pings device to keep connection alive
        if (getsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &val, &len) == 0) {
            log_mqtt(LOG_INFO, TAG, true, "Keep alive : %d", val);
        } else {
            log_mqtt(LOG_ERROR, TAG, true, "Error getting keep alive option socket info");
        }
        //allow receive / send broadcast messages (255.255.255.255)
        if (getsockopt(sock, SOL_SOCKET, SO_BROADCAST, &val, &len) == 0) {
            log_mqtt(LOG_INFO, TAG, true, "Broadcast : %d", val);
        } else {
            log_mqtt(LOG_ERROR, TAG, true, "Error getting broadcast option socket info");
        }
        //length of receiver buffer
        if (getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &val, &len) == 0) {
            log_mqtt(LOG_INFO, TAG, true, "Receive buffer size : %d", val);
        } else {
            log_mqtt(LOG_ERROR, TAG, true, "Error getting receive buffer option socket info");
        }

        socklen_t lenTimeout = sizeof(timeout);
        //blocking timeout for sending
        if (getsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, &lenTimeout) == 0) {
            log_mqtt(LOG_INFO, TAG, true, "Send timeout: %ld.%06ld sec\n", timeout.tv_sec, timeout.tv_usec);
        } else {
            log_mqtt(LOG_ERROR, TAG, true, "Error getting send timeout option socket info");
        }
        //blocking timeout for receiving
        if (getsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, &lenTimeout) == 0) {
            log_mqtt(LOG_INFO, TAG, true, "Receive timeout: %ld.%06ld sec\n", timeout.tv_sec, timeout.tv_usec);
        } else {
            log_mqtt(LOG_ERROR, TAG, true, "Error getting receive timeout option socket info");
        }

        //socket type : tcp, udp, raw ip
        if (getsockopt(sock, SOL_SOCKET, SO_TYPE, &val, &len) == 0) {
            log_mqtt(LOG_INFO, TAG, true, "Socket type : %s", sock_type_to_str(val));
        } else {
            log_mqtt(LOG_ERROR, TAG, true, "Error getting socket type option socket info");
        }
        //udp no checksum (for perf, but not safe at all)
        if (getsockopt(sock, SOL_SOCKET, SO_NO_CHECK, &val, &len) == 0) {
            log_mqtt(LOG_INFO, TAG, true, "No checksum : %d", val);
        } else {
            log_mqtt(LOG_ERROR, TAG, true, "Error getting no checksum option socket info");
        }
#endif

        //options handled by lwip socket (see in header)
        //setsockopt (sock, SOL_SOCKET, SO_ERROR); socket error handling

        //bind socket to port
        int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            log_mqtt(LOG_ERROR, TAG, true, "Socket unable to bind: errno %d", errno);
        }
        log_mqtt(LOG_INFO, TAG, true, "Socket bound, port %d", PORT);

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t socklen = sizeof(source_addr);

#if PACKET_DEBUG_ACTIVATED
        //infos for debug message (ipv4 only)
        struct iovec iov;
        struct msghdr msg;
        u8_t cmsg_buf[CMSG_SPACE(sizeof(struct in_pktinfo))];

        iov.iov_base = temp_buffer;
        iov.iov_len = sizeof(temp_buffer);
        msg.msg_control = cmsg_buf;
        msg.msg_controllen = sizeof(cmsg_buf);
        msg.msg_flags = 0;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_name = (struct sockaddr *)&source_addr;
        msg.msg_namelen = socklen;
#endif

        while (1) {
            log_mqtt(LOG_DEBUG, TAG, false, "Waiting for data");

#if PACKET_DEBUG_ACTIVATED
            //receive message for debug (ipv4 only)
            int len = recvmsg(sock, &msg, 0);

#else

            //wait to receive data, store source socket addr
            int len = recvfrom(sock, temp_buffer, sizeof(temp_buffer), 0, (struct sockaddr *)&source_addr, &socklen);
#endif

            // Error occurred during receiving
            if (len < 0) {
                log_mqtt(LOG_ERROR, TAG, true, "recvfrom failed: errno %d", errno);
                break;
            } else { // Data received

#if PACKET_DEBUG_ACTIVATED
                //for debug (ipv4 only)
                for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
                    log_mqtt(LOG_INFO, TAG, false, "cmsg_level=%s, cmsg_type=%s, cmsg_len=%d",
                            cmsg_protocol_to_str(cmsg->cmsg_level),
                            cmsg_protocol_spe_to_str(cmsg->cmsg_type), cmsg->cmsg_len);

                    if (cmsg->cmsg_level == IPPROTO_IP) {
                        switch (cmsg->cmsg_type) {
                            case IP_PKTINFO: {
                                struct in_pktinfo *pktinfo = (struct in_pktinfo *)CMSG_DATA(cmsg);
                                log_mqtt(LOG_INFO, TAG, true, "IP_PKTINFO -> dest IP: %s, ifindex: %u",
                                        inet_ntoa(pktinfo->ipi_addr), pktinfo->ipi_ifindex);
                                break;
                            }
                            case IP_TTL: {
                                int ttl = *(int *)CMSG_DATA(cmsg);
                                log_mqtt(LOG_INFO, TAG, true, "IP_TTL -> %d", ttl);
                                break;
                            }
                            case IP_TOS: {
                                int tos = *(int *)CMSG_DATA(cmsg);
                                log_mqtt(LOG_INFO, TAG, true, "IP_TOS -> 0x%02X", tos);
                                break;
                            }
                            default:
                                log_mqtt(LOG_INFO, TAG, true, "Unknown IP cmsg_type=%d", cmsg->cmsg_type);
                                break;
                        }
                    } else {
                        log_mqtt(LOG_INFO, TAG, true, "Unknown cmsg_level=%d", cmsg->cmsg_level);
                    }
                }

                //raw
                // 0..3 : cmsg_len
                // 4..7 : level
                // 8..11 : type
                // 12..15 : ifindex
                // 16..19 : ip
                u8_t *cmsg_data = (u8_t *)msg.msg_control;
                for (int i = 0; i < msg.msg_controllen; i++) {
                    log_mqtt(LOG_INFO, TAG, false, "cmsg raw[%d]=0x%02X", i, cmsg_data[i]);
                }
#endif

#if TIME_STATS
                int64_t current_time = esp_timer_get_time(); // ms
                if (last_packet_time != 0) {
                    int64_t delta = current_time - last_packet_time; // delta us
                    sum_intervals += delta;
                    packet_count++;

                    int64_t average_interval = sum_intervals / packet_count; // average in us
                    log_mqtt(LOG_INFO, TAG, false, "Interval: %lld us, Average: %lld us", delta, average_interval);
                }
                last_packet_time = current_time;
#endif

#if ADDRESS_DEBUG

                //PF here
                //PF = Protocol (old)
                //AF = Address (new ones)
                //PF = AF usually in terms of value
                log_mqtt(LOG_INFO, TAG, true, "Client family PF : %s", addr_family_pf_to_str(source_addr.ss_family)); //PF_INET
                struct sockaddr_in * source_addr_ip4 = ((struct sockaddr_in *)&source_addr); //cast to IPv4

                char ip_buf[16]; //IPv4
                inet_ntoa_r(source_addr_ip4->sin_addr, ip_buf, sizeof(ip_buf));
                log_mqtt(LOG_INFO, TAG, true, "IP : %s", ip_buf);
                log_mqtt(LOG_INFO, TAG, true, "Family : %s", addr_family_to_str(source_addr_ip4->sin_family));
                log_mqtt(LOG_INFO, TAG, true, "Port : %d", ntohs(source_addr_ip4->sin_port));
                
#endif
                esp_err_t esp_err;
                command_type_t type;
                esp_err = get_cmd_type(temp_buffer, &type);
                if (esp_err != ESP_OK) {
                    log_mqtt(LOG_ERROR, TAG, true, "Error (%s) getting command type", 
                            esp_err_to_name(esp_err));
                }
#if INFO_LOGS
                nb_packets_received++;
                log_mqtt(LOG_INFO, TAG, false, "Received %d bytes: packet number : %d",
                    len, nb_packets_received);
#endif
                switch (type) {
                    case CMD_GAMEPAD: //gamepad type control

                        gamepad_t gamepad;
                        esp_err = gamepad_from_buffer(temp_buffer, &gamepad);
                        if (esp_err != ESP_OK) {
                            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) getting gamepad from buffer", 
                                    esp_err_to_name(esp_err));
                            break;
                        }
#if INFO_LOGS
                        dump_gamepad(&gamepad);
#endif

                        esp_err = apply_gamepad_commands(&gamepad);
                        if (esp_err != ESP_OK) {
                            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) applying gamepad commands", 
                                    esp_err_to_name(esp_err));
                            break;
                        }
                        break;
                    
                    case CMD_ANDROID: //android type control

                        android_t android;
                        esp_err = android_from_buffer(temp_buffer, &android);
                        if (esp_err != ESP_OK) {
                            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) getting android from buffer", 
                                    esp_err_to_name(esp_err));
                            break;
                        }
#if INFO_LOGS
                        dump_android(&android);
#endif
                        
                        esp_err = apply_android_commands(&android);
                        if (esp_err != ESP_OK) {
                            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) applying android commands", 
                                    esp_err_to_name(esp_err));
                            break;
                        }
                        
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
                ledc_motor(0);
            }
        }
    vTaskDelete(NULL);
}

/**
 * Function to init server : 
 * Create task udp server with 15 priority & on core 1
 */
void udp_server_init()
{
    xTaskCreatePinnedToCore(udp_server_task, "udp_server", 8192, (void*)AF_INET, 15, NULL, 1);
}