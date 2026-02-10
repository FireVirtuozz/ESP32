#include "wsLib.h"
#include <esp_log.h>
#include <string.h>
#include <stdlib.h>
#include <esp_http_server.h>
#include "ledLib.h"
#include "mqttLib.h"

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

static const char *TAG = "ws_library"; // tag of this library
static httpd_handle_t server = NULL; // handler for server : configure server http

/**
 * callback fonction called at each request
 * @param req request info (uri, method, header..)
 * @return success or error
 */
static esp_err_t ws_handler(httpd_req_t *req)
{
    //if get method, client connected and return ok
    if (req->method == HTTP_GET) { 
        log_mqtt(LOG_INFO, TAG, true, "Client connected");
        return ESP_OK;
    }

    //initialize structure for ws frame
    httpd_ws_frame_t ws_pkt = {0};

    //read header of request (0 length) to get the length
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    //once length got, allocate memory to store the payload
    ws_pkt.payload = malloc(ws_pkt.len + 1); //+1 for null terminator
    if (!ws_pkt.payload) return ESP_ERR_NO_MEM; //check if memory ok

    //read data of whole message
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    
    //free buffer if error
    if (ret != ESP_OK) {
        free(ws_pkt.payload);
        return ret;
    }

    //adding end character to have a valid string
    ws_pkt.payload[ws_pkt.len] = '\0'; 

    //log for debug
    log_mqtt(LOG_INFO, TAG, false, "Received: %s", (char *)ws_pkt.payload);

    //compare messages to give instructions
    if (strcmp((char *)ws_pkt.payload, "LED_ON") == 0) {
        led_on();
    } else if (strcmp((char *)ws_pkt.payload, "LED_OFF") == 0) {
        led_off();
    } else if (strcmp((char *)ws_pkt.payload, "LED_TOGGLE") == 0) {
        led_toggle();
    }

    //prepare response to client
    httpd_ws_frame_t resp = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)"OK", //cast to fit type
        .len = 2
    };

    //return respond to client (req has the client info)
    ret = httpd_ws_send_frame(req, &resp);

    //free memory buffer
    free(ws_pkt.payload);
    return ret;
}

/**
 * callback fonction called at each request for commands by controller
 * @param req request info (uri, method, header..)
 * @return success or error
 */
static esp_err_t controller_handler(httpd_req_t *req)
{
    //using static buffer, not malloc, for better performance, as the size is constant
    /*static int8_t commands_buffer[7]; //6 axis, 1 for buttons*/

    static uint8_t temp_buffer[7]; //avoid doing malloc, as we know the size is 7

    //if get method, client connected and return ok
    if (req->method == HTTP_GET) { 
        log_mqtt(LOG_INFO, TAG, true, "Client connected");
        return ESP_OK;
    }

    //initialize structure for ws frame
    httpd_ws_frame_t ws_pkt = {0};

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);

    //ws_pkt.payload = malloc(ws_pkt.len);
    ws_pkt.payload = temp_buffer;

    //read data of whole message
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        //free(ws_pkt.payload);
        return ret;
    }

    log_mqtt(LOG_DEBUG, TAG, false, "Frame size : %d", ws_pkt.len);
    log_mqtt(LOG_DEBUG, TAG, false, "Frame type : %d", ws_pkt.type);
    log_mqtt(LOG_DEBUG, TAG, false, "Frame final : %d", ws_pkt.final);
    log_mqtt(LOG_DEBUG, TAG, false, "Frame frag : %d", ws_pkt.fragmented);

    /*
    if (ws_pkt.len != 7) {
        log_mqtt(LOG_WARN, TAG, true, "Unexpected size of ws frame : %d", ws_pkt.len);
    }
    */
    
    int8_t *p = (int8_t *)ws_pkt.payload;
    log_mqtt(LOG_DEBUG, TAG, false, "Gamepad axes raw: [%d,%d,%d,%d,%d,%d]", 
         p[0], p[1], p[2],
         p[3], p[4], p[5]);
    log_mqtt(LOG_DEBUG, TAG, false, "Gamepad button raw: [%d,%d,%d,%d,%d,%d,%d,%d]", 
        (p[6] & 0x01) ? 1 : 0, (p[6] & 0x02) ? 1 : 0,
        (p[6] & 0x04) ? 1 : 0, (p[6] & 0x08) ? 1 : 0,
        (p[6] & 0x10) ? 1 : 0, (p[6] & 0x20) ? 1 : 0,
        (p[6] & 0x40) ? 1 : 0, (p[6] & 0x80) ? 1 : 0);

    ledc_angle((int16_t)((p[0] + 100) * 9 / 10)); //left_x
    
    if (p[5] > -95) {
        ledc_motor((int16_t)((p[5] + 100) / 2)); //right_trigger
    } else if (p[4] > -95) {
        ledc_motor((int16_t)((p[4] + 100) / (-2))); //left_trigger
    } else {
        ledc_motor(0);
    }

    /*
    //prepare response to client
    httpd_ws_frame_t resp = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)"OK", //cast to fit type
        .len = 2
    };

    //return respond to client (req has the client info)
    ret = httpd_ws_send_frame(req, &resp);
    */

    //free(ws_pkt.payload);
    return ret;
}


// Uri websocket : get, ws_handler
static httpd_uri_t ws_uri = {
    .uri        = "/ws/command",
    .method     = HTTP_GET,
    .handler    = ws_handler,
    .is_websocket = true
};

// Uri websocket : get, ws_handler
static httpd_uri_t controller_uri = {
    .uri        = "/ws/controller",
    .method     = HTTP_GET,
    .handler    = controller_handler,
    .is_websocket = true
};

/**
 * Function to init server : 
 * Init server handler with default config on port 80 and register uri handlers
 */
void ws_server_init()
{
    if (server != NULL) {
        log_mqtt(LOG_WARN, TAG, true, "WS server already initialized");
    }

    //get default config, on port 80
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    //start server on server handler with default config
    if (httpd_start(&server, &config) == ESP_OK) {
        //register uri handler from before
        httpd_register_uri_handler(server, &ws_uri);
        httpd_register_uri_handler(server, &controller_uri);
        log_mqtt(LOG_INFO, TAG, true, "WS server started");
    } else {
        log_mqtt(LOG_ERROR, TAG, true, "Error on WS server startup");
    }
}

/**
 * Send text
 * @param msg message char*
 */
esp_err_t ws_send_text(const char *msg)
{
    //check if server not NULL
    if (!server) return ESP_FAIL;

    //declare frame structure to send
    httpd_ws_frame_t frame;
    //alloc memory and initialize to 0
    memset(&frame, 0, sizeof(frame));
    //set frame content
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = (uint8_t *)msg;
    frame.len = strlen(msg);

    //send frame to all clients
    //if server handler : all clients
    //if request req : one client
    return httpd_ws_send_frame(server, &frame);
}
