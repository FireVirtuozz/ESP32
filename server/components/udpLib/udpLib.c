#include "udpLib.h"
#include <esp_log.h>
#include <string.h>
#include <stdlib.h>
#include "ledLib.h"
#include "logLib.h"
#include "cmdLib.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "freertos/idf_additions.h"

#if CONFIG_PACKET_DEBUG
#include "esp_timer.h"
#endif

#define PORT 3333

#define UDP_COMMANDS_TIMEOUT 900000 //us, no more than 1s = 1000000

//TODO : IPv6 support, length commands buffer check, security

static const char *TAG = "udp_library"; // tag of this library

#if CONFIG_PACKET_DEBUG
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

#if CONFIG_PACKET_DEBUG
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

#if CONFIG_PACKET_DEBUG
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

#if CONFIG_PACKET_DEBUG
    static int nb_packets_received = 0;
#endif

    while (1) {

#if CONFIG_PACKET_DEBUG
        int64_t last_packet_time = 0;
        int64_t sum_intervals = 0;
        int packet_count = 0;

        log_msg(TAG, "Address family : %s", addr_family_to_str(addr_family));
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
#if CONFIG_PACKET_DEBUG
            log_msg(TAG, "IP protocol : %s", cmsg_protocol_to_str(ip_protocol));
            log_msg(TAG, "IP : %s", source_ip_to_str(ntohl(dest_addr_ip4->sin_addr.s_addr)));
            log_msg(TAG, "Family : %s", addr_family_to_str(dest_addr_ip4->sin_family));
            log_msg(TAG, "Port : %d", ntohs(dest_addr_ip4->sin_port));
#endif
        }


        //create socket 
        //inline : implemented in lwip's socket header for better performance
        //addr family : ipv4
        //sock_dgram : udp
        //ip protocol : auto
        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol); //create socket    
        if (sock < 0) {
            log_msg(TAG, "Unable to create socket: %d, %s", errno, strerror(errno));
            break;
        }
        log_msg(TAG, "Socket created");

#if CONFIG_PACKET_DEBUG
        //set receive packt info for debug (ipv4 only)
        int enable = 1;
        lwip_setsockopt(sock, IPPROTO_IP, IP_PKTINFO, &enable, sizeof(enable));
#endif

        // Set timeout : 10 seconds
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = UDP_COMMANDS_TIMEOUT;
        setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout); //socket option timeout

#if CONFIG_PACKET_DEBUG
        int val;
        socklen_t len = sizeof(val);
        //reuse port if already used in TIME_WAIT (state TCP when connection closed)
        if (getsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &val, &len) == 0) {
            log_msg(TAG, "Reuse address : %d", val);
        } else {
            log_msg(TAG, "Error getting reuse address option socket info");
        }
        //in TCP : pings device to keep connection alive
        if (getsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &val, &len) == 0) {
            log_msg(TAG, "Keep alive : %d", val);
        } else {
            log_msg(TAG, "Error getting keep alive option socket info");
        }
        //allow receive / send broadcast messages (255.255.255.255)
        if (getsockopt(sock, SOL_SOCKET, SO_BROADCAST, &val, &len) == 0) {
            log_msg(TAG, "Broadcast : %d", val);
        } else {
            log_msg(TAG, "Error getting broadcast option socket info");
        }
        //length of receiver buffer
        if (getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &val, &len) == 0) {
            log_msg(TAG, "Receive buffer size : %d", val);
        } else {
            log_msg(TAG, "Error getting receive buffer option socket info");
        }

        socklen_t lenTimeout = sizeof(timeout);
        //blocking timeout for sending
        if (getsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, &lenTimeout) == 0) {
            log_msg(TAG, "Send timeout: %ld.%06ld sec\n", timeout.tv_sec, timeout.tv_usec);
        } else {
            log_msg(TAG, "Error getting send timeout option socket info");
        }
        //blocking timeout for receiving
        if (getsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, &lenTimeout) == 0) {
            log_msg(TAG, "Receive timeout: %ld.%06ld sec\n", timeout.tv_sec, timeout.tv_usec);
        } else {
            log_msg(TAG, "Error getting receive timeout option socket info");
        }

        //socket type : tcp, udp, raw ip
        if (getsockopt(sock, SOL_SOCKET, SO_TYPE, &val, &len) == 0) {
            log_msg(TAG, "Socket type : %s", sock_type_to_str(val));
        } else {
            log_msg(TAG, "Error getting socket type option socket info");
        }
        //udp no checksum (for perf, but not safe at all)
        if (getsockopt(sock, SOL_SOCKET, SO_NO_CHECK, &val, &len) == 0) {
            log_msg(TAG, "No checksum : %d", val);
        } else {
            log_msg(TAG, "Error getting no checksum option socket info");
        }
#endif

        //options handled by lwip socket (see in header)
        //setsockopt (sock, SOL_SOCKET, SO_ERROR); socket error handling

        //bind socket to port
        int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            log_msg(TAG, "Socket unable to bind: errno %d", errno);
        }
        log_msg(TAG, "Socket bound, port %d", PORT);

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t socklen = sizeof(source_addr);

#if CONFIG_PACKET_DEBUG
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
            log_msg(TAG, "Waiting for data");

#if CONFIG_PACKET_DEBUG
            //receive message for debug (ipv4 only)
            int len = recvmsg(sock, &msg, 0);

#else

            //wait to receive data, store source socket addr
            int len = recvfrom(sock, temp_buffer, sizeof(temp_buffer), 0, (struct sockaddr *)&source_addr, &socklen);
#endif

            // Error occurred during receiving
            if (len < 0) {
                log_msg(TAG, "recvfrom failed: errno %d", errno);
                break;
            } else { // Data received

#if CONFIG_PACKET_DEBUG
                //for debug (ipv4 only)
                for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
                    log_msg(TAG, "cmsg_level=%s, cmsg_type=%s, cmsg_len=%d",
                            cmsg_protocol_to_str(cmsg->cmsg_level),
                            cmsg_protocol_spe_to_str(cmsg->cmsg_type), cmsg->cmsg_len);

                    if (cmsg->cmsg_level == IPPROTO_IP) {
                        switch (cmsg->cmsg_type) {
                            case IP_PKTINFO: {
                                struct in_pktinfo *pktinfo = (struct in_pktinfo *)CMSG_DATA(cmsg);
                                log_msg(TAG, "IP_PKTINFO -> dest IP: %s, ifindex: %u",
                                        inet_ntoa(pktinfo->ipi_addr), pktinfo->ipi_ifindex);
                                break;
                            }
                            case IP_TTL: {
                                int ttl = *(int *)CMSG_DATA(cmsg);
                                log_msg(TAG, "IP_TTL -> %d", ttl);
                                break;
                            }
                            case IP_TOS: {
                                int tos = *(int *)CMSG_DATA(cmsg);
                                log_msg(TAG, "IP_TOS -> 0x%02X", tos);
                                break;
                            }
                            default:
                                log_msg(TAG, "Unknown IP cmsg_type=%d", cmsg->cmsg_type);
                                break;
                        }
                    } else {
                        log_msg(TAG, "Unknown cmsg_level=%d", cmsg->cmsg_level);
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
                    log_msg(TAG, "cmsg raw[%d]=0x%02X", i, cmsg_data[i]);
                }

                int64_t current_time = esp_timer_get_time(); // ms
                if (last_packet_time != 0) {
                    int64_t delta = current_time - last_packet_time; // delta us
                    sum_intervals += delta;
                    packet_count++;

                    int64_t average_interval = sum_intervals / packet_count; // average in us
                    log_msg(TAG, "Interval: %lld us, Average: %lld us", delta, average_interval);
                }
                last_packet_time = current_time;

                //PF here
                //PF = Protocol (old)
                //AF = Address (new ones)
                //PF = AF usually in terms of value
                log_msg(TAG, "Client family PF : %s", addr_family_pf_to_str(source_addr.ss_family)); //PF_INET
                struct sockaddr_in * source_addr_ip4 = ((struct sockaddr_in *)&source_addr); //cast to IPv4

                char ip_buf[16]; //IPv4
                inet_ntoa_r(source_addr_ip4->sin_addr, ip_buf, sizeof(ip_buf));
                log_msg(TAG, "IP : %s", ip_buf);
                log_msg(TAG, "Family : %s", addr_family_to_str(source_addr_ip4->sin_family));
                log_msg(TAG, "Port : %d", ntohs(source_addr_ip4->sin_port));
                
                nb_packets_received++;
                log_msg(TAG, "Received %d bytes: packet number : %d",
                    len, nb_packets_received);
#endif

                cmd_dispatch(temp_buffer);
            }
                
            }

            //shutdown socket if error
            if (sock != -1) {
                log_msg(TAG, "Shutting down socket and restarting...");
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
    BaseType_t res = xTaskCreatePinnedToCore(udp_server_task, "udp_server", 8192, (void*)AF_INET, 15, NULL, 0);
    if (res != pdPASS) {
        log_msg(TAG, "Error (%d) create UDP task on core 1", res);
    }
    
}

#define HOST_IP_ADDR "192.168.4.2"
#define HOST_PORT 34254

static QueueHandle_t queue_send = NULL;

static void udp_client_task(void *pvParameters)
{
    int addr_family = 0;
    int ip_protocol = 0;

    if (queue_send != NULL) {
        log_msg_lvl(ESP_LOG_WARN, TAG, "UDP client aldready initialized");
        return;
    }

    queue_send = xQueueCreate(32, sizeof(udp_msg_t));
    if (queue_send == NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error creating UDP queue");
        return;
    }

    //setup socket
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(HOST_IP_ADDR);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(HOST_PORT);
    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;

    int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
    if (sock < 0) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Unable to create socket: errno %d", errno);
        return;
    }

    log_msg(TAG, "Socket created, sending to %s:%d", HOST_IP_ADDR, HOST_PORT);

    udp_msg_t msg_temp;
    while (xQueueReceive(queue_send, &msg_temp, portMAX_DELAY) == pdTRUE) {
        int err;
        err = sendto(sock, msg_temp.data, msg_temp.len, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            //log_msg_lvl(ESP_LOG_ERROR, TAG, "Error occurred during sending: errno %d", errno);
        } else {
            log_msg(TAG, "Message sent");
        }
    }
    vTaskDelete(NULL);
}

void send_udp_msg(udp_msg_t *msg){
    if (queue_send == NULL || msg == NULL) {
        return;
    }
    xQueueSend(queue_send, msg, 0);
}

#define VIDEO_PORT 34255

#define HEADER_UDP_VID_SIZE (sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint8_t))
#define MAX_VID_PAYLOAD_SIZE (UDP_MAX_SIZE - HEADER_UDP_VID_SIZE)

typedef struct header_udp_vid_st {
    uint32_t frame_id;
    uint8_t frag_total;
    uint8_t frag_idx;
} header_udp_vid_t;

static void header_serialize(const header_udp_vid_t *hdr, uint8_t *buf) {
    buf[0] = (hdr->frame_id >> 24) & 0xFF;
    buf[1] = (hdr->frame_id >> 16) & 0xFF;
    buf[2] = (hdr->frame_id >> 8)  & 0xFF;
    buf[3] =  hdr->frame_id        & 0xFF;
    buf[4] = hdr->frag_total;
    buf[5] = hdr->frag_idx;
}

static QueueHandle_t queue_send_video = NULL;

static void udp_client_task_video(void *pvParameters)
{
    int addr_family = 0;
    int ip_protocol = 0;

    if (queue_send_video != NULL) {
        log_msg_lvl(ESP_LOG_WARN, TAG, "UDP client aldready initialized");
        return;
    }

    queue_send_video = xQueueCreate(32, sizeof(udp_msg_vid_t));
    if (queue_send_video == NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error creating UDP queue");
        return;
    }

    //setup socket
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(HOST_IP_ADDR);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(VIDEO_PORT);
    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;

    int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
    if (sock < 0) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Unable to create socket: errno %d", strerror(errno));
        return;
    }

    log_msg(TAG, "Socket created, sending to %s:%d", HOST_IP_ADDR, HOST_PORT);

    udp_msg_vid_t msg_tmp;
    uint32_t total_size_payload;
    header_udp_vid_t hd = {0};
    uint8_t buf[UDP_MAX_SIZE];
    uint32_t offset;
    uint16_t payload_size;
    uint8_t logged = 0;

    while (xQueueReceive(queue_send_video, &msg_tmp, portMAX_DELAY) == pdTRUE) {
        total_size_payload = msg_tmp.len;
        //avoid perfect multiples
        hd.frag_total = (total_size_payload + MAX_VID_PAYLOAD_SIZE - 1)/ MAX_VID_PAYLOAD_SIZE ;
        hd.frag_idx = 0;

        if (!logged) {
            for (int i = 0; i < msg_tmp.len; i += 16) {
                ESP_LOG_BUFFER_HEX(TAG, msg_tmp.data + i, (msg_tmp.len - i) < 16 ? (msg_tmp.len - i) : 16);
            }
            logged = 1;
        }

        for (uint16_t i = 0; i < hd.frag_total; i++) {

            offset = i * MAX_VID_PAYLOAD_SIZE;
            hd.frag_idx = i;
            payload_size = (i == hd.frag_total - 1) ? (total_size_payload - offset) : MAX_VID_PAYLOAD_SIZE;

            header_serialize(&hd, buf);
            memcpy(&buf[HEADER_UDP_VID_SIZE], msg_tmp.data + offset, payload_size);
            sendto(sock, buf, payload_size + HEADER_UDP_VID_SIZE, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        }
        hd.frame_id++;
        free(msg_tmp.data);
    }
    vTaskDelete(NULL);
}

void send_udp_jpeg(udp_msg_vid_t *msg) {
    if (msg == NULL) {
        return;
    }
    if (queue_send_video == NULL) {
        log_msg_lvl(ESP_LOG_WARN, TAG, "Video queue unvalid, freeing data");
        free(msg->data);
        return;
    }
    if (xQueueSend(queue_send_video, msg, 0) != pdTRUE) {
        log_msg_lvl(ESP_LOG_WARN, TAG, "Video queue full, freeing data");
        free(msg->data);
    }
}

/**
 * Function to init server : 
 * Create task udp server with 15 priority & on core 1
 */
void udp_client_init()
{
    BaseType_t res = xTaskCreate(udp_client_task, "udp_client", 8192, (void*)AF_INET, 4, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) create client UDP task", res);
    }

    res = xTaskCreate(udp_client_task_video, "udp_client_video", 8192, (void*)AF_INET, 4, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) create client video UDP task", res);
    }
    
}