#include "wsLib.h"


httpd_handle_t server = NULL;


static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI("WS", "Client connecté");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));

    // 1. Recevoir la taille du frame
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    // 2. Allouer le payload
    ws_pkt.payload = malloc(ws_pkt.len + 1);
    if (!ws_pkt.payload) return ESP_ERR_NO_MEM;

    // 3. Recevoir le contenu
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        free(ws_pkt.payload);
        return ret;
    }

    ws_pkt.payload[ws_pkt.len] = 0; // Null-terminer
    ESP_LOGI("WS", "Reçu: %s", (char *)ws_pkt.payload);

    // 4. Echo au client
    ret = httpd_ws_send_frame(req->handle, &ws_pkt);
    free(ws_pkt.payload);
    return ret;
}



// URI WS
static httpd_uri_t ws_uri = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = ws_handler,
    .is_websocket = true
};

void ws_server_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &ws_uri);
        ESP_LOGI("WS", "Serveur WS démarré");
    } else {
        ESP_LOGE("WS", "Erreur démarrage serveur WS");
    }
}

// Optionnel : envoyer un message à tous les clients
esp_err_t ws_send_text(const char *msg)
{
    if (!server) return ESP_FAIL;

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)msg,
        .len = strlen(msg)
    };

    // Ici pour simplifier on envoie au client 0 (tu peux itérer si multi-clients)
    return httpd_ws_send_frame(server->hd, &frame);
}
