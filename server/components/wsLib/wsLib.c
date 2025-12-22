#include "wsLib.h"
#include <esp_log.h>
#include <string.h>
#include <stdlib.h>
#include <esp_http_server.h>
#include "ledLib.h"

static const char *TAG = "WS";
static httpd_handle_t server = NULL;

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Client connecté");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt = {0};

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    ws_pkt.payload = malloc(ws_pkt.len + 1);
    if (!ws_pkt.payload) return ESP_ERR_NO_MEM;

    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        free(ws_pkt.payload);
        return ret;
    }

    ws_pkt.payload[ws_pkt.len] = '\0';  // IMPORTANT

    ESP_LOGI(TAG, "Reçu: %s", (char *)ws_pkt.payload);

    if (strcmp((char *)ws_pkt.payload, "LED_ON") == 0) {
        led_on();
    } else if (strcmp((char *)ws_pkt.payload, "LED_OFF") == 0) {
        led_off();
    } else if (strcmp((char *)ws_pkt.payload, "LED_TOGGLE") == 0) {
        led_toggle();
    }

    // Réponse ACK
    httpd_ws_frame_t resp = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)"OK",
        .len = 2
    };

    ret = httpd_ws_send_frame(req, &resp);

    free(ws_pkt.payload);
    return ret;
}


// Déclaration URI WebSocket
static httpd_uri_t ws_uri = {
    .uri        = "/ws",
    .method     = HTTP_GET,
    .handler    = ws_handler,
    .is_websocket = true
};

void ws_server_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &ws_uri);
        ESP_LOGI(TAG, "Serveur WS démarré");
    } else {
        ESP_LOGE(TAG, "Erreur démarrage serveur WS");
    }
}

esp_err_t ws_send_text(const char *msg)
{
    if (!server) return ESP_FAIL;

    httpd_ws_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = (uint8_t *)msg;
    frame.len = strlen(msg);

    return httpd_ws_send_frame(server, &frame);
}
