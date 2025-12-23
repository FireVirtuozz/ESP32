#include "wifiLib.h"
#include <esp_wifi.h>

/**
 * WiFi library
 *
 * - Event group handler for WiFi events
 * 
 * - WiFi station initialization:
 *   - Initialize NVS : store persistent data (flash not ram) --> done in nvs_init
 *   - Initialize netif (network interface) (TCP/IP stack..) and event loop (messages bus)
 *   - Configure and initialize WiFi driver (hardware of ESP32, handle frames..)
 *   - Register WiFi and IP event handlers (system call for handlers)
 *   - Configure station mode and start WiFi
 *   - Wait until IP address is obtained
 *
 * - WiFi event handler:
 *   - Handle events by type, ID, and data
 *   - Start WiFi connection
 *   - Reconnect on disconnection
 *   - Handle IP address assigned by the router
 */

//wifi credentials
#define WIFI_SSID "freebox_isidor"
#define WIFI_PASS "casanova1664"

//handler for wifi event
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0 // Bit indicating WiFi connection, blocks application until connected

//Buffer for IP address
static char s_ip_str[16];

/**
 * Event handler:
 * - Handle first connection
 * - Try to reconnect if disconnected
 * - Get IP from router and unblock waiting tasks
 * @param arg
 * @param event_base type of event (wifi, IP..)
 * @param event_id id of event (start, end..)
 * @param event_data data of event
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    //if wifi starting event
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect(); // Start wifi connection : send request to router
    //if wifi disconnect event, trying to reconnect
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI("WIFI", "Disconnected, trying to reconnect..");
        esp_wifi_connect();
    //if router assigned an IP address to ESP (triggered by DHCP netif when connection ok by router)
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        //get IP from data : cast
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        //log wifi connected IP
        ESP_LOGI("WIFI", "Connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));

        // Store IP address as string for later use
        snprintf(s_ip_str, sizeof(s_ip_str),
                 IPSTR, IP2STR(&event->ip_info.ip));

        //update global handler to wifi connected to unlock tasks waiting for wifi
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * Initialize ESP WiFi in station mode
 * -Initialize NVS to store WIFI data --> done in nvs_init
 * -Initialize netif for TCP/IP and events
 * -Load default config, initialize driver
 * -register wifi event handlers
 * -Configure wifi station (STA) and start it
 * -Wait wait until IP received
 */
void wifi_init_sta(void)
{
    /*
    //Initialize NVS (non volatile storage) to store data from wifi
    esp_err_t ret = nvs_flash_init();
    //if error : erase & init clean
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
        */

    //Initialize netif TCP/IP
    esp_netif_init();
    //Create global handler events for wifi
    s_wifi_event_group = xEventGroupCreate();
    //Create system event loop
    esp_event_loop_create_default();
    //Create wifi network in station mode
    esp_netif_create_default_wifi_sta();

    //Load default config and initialize driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    //register handler for all wifi events
    esp_event_handler_instance_t instance_any_id;
    //register handler for IP event (GOT_IP)
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler, NULL, &instance_got_ip);

    //Config station : copy SSID and password
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));

    //Configure esp in station mode
    esp_wifi_set_mode(WIFI_MODE_STA);
    //apply wifi config to station
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    //start wifi : triggers WIFI_EVENT_STA_START handler
    esp_wifi_start();

    //debug
    ESP_LOGI("WIFI", "Connection to %s...", WIFI_SSID);

    //Waiting for connection until WIFI_CONNECTED_BIT is activated (if init in main, it will wait..)
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);
}

/**
 * Function to get the IP if someone wants to
 */
const char* wifi_get_ip(void)
{
    return s_ip_str;
}
