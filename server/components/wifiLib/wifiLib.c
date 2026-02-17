#include "wifiLib.h"
#include <esp_wifi.h>
#include "mqttLib.h"
#include <stdarg.h>

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

 //scan AP max number
#define DEFAULT_SCAN_LIST_SIZE 16

//ESP AP Config
#define ESP_SSID "big_esp"
#define ESP_PASS "espdegigachad"
#define ESP_MAX_CONNECTION 4

/*DHCP server option flag*/
#define DHCPS_OFFER_DNS             0x02

#define MAX_STR_LINES 16
#define MAX_LINE_LEN 128

typedef struct {
    char lines[MAX_STR_LINES][MAX_LINE_LEN];
    int count;
} sta_info_strings_t;

//wifi known networks
typedef struct {
    const char* ssid;
    const char* password;
    uint8_t priority;
} wifi_network_t;

wifi_network_t known_networks[] = {
    {"freebox_isidor", "casanova1664", 0}, 
    {"tchomec", "BahOuais", 1},
};

static bool scanning = false;

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi_library";

//Buffer for IP address
static char s_ip_str[16];

/*
=====================================================================
FUNCTION CONVERT TO STR
=====================================================================
*/

//function to print the wifi authentication mode
static const char *get_authmode_str(int authmode)
{
    switch (authmode) {
    case WIFI_AUTH_OPEN:
        return "WIFI_AUTH_OPEN";
    case WIFI_AUTH_OWE:
        return "WIFI_AUTH_OWE";
    case WIFI_AUTH_WEP:
        return "WIFI_AUTH_WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WIFI_AUTH_WPA_PSK";
    case WIFI_AUTH_WPA2_PSK:
        return "WIFI_AUTH_WPA2_PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WIFI_AUTH_WPA_WPA2_PSK";
    case WIFI_AUTH_ENTERPRISE:
        return "WIFI_AUTH_ENTERPRISE";
    case WIFI_AUTH_WPA3_PSK:
        return "WIFI_AUTH_WPA3_PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WIFI_AUTH_WPA2_WPA3_PSK";
    case WIFI_AUTH_WPA3_ENTERPRISE:
        return "WIFI_AUTH_WPA3_ENTERPRISE";
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
        return "WIFI_AUTH_WPA2_WPA3_ENTERPRISE";
    case WIFI_AUTH_WPA3_ENT_192:
        return "WIFI_AUTH_WPA3_ENT_192";
    default:
        return "WIFI_AUTH_UNKNOWN";
    }
}

//function to print cipher pair (encryption)
static const char *get_cipher_pair_str(int pairwise) {
    switch (pairwise) {
    case WIFI_CIPHER_TYPE_NONE:
        return "WIFI_CIPHER_TYPE_NONE";
    case WIFI_CIPHER_TYPE_WEP40:
        return "WIFI_CIPHER_TYPE_WEP40";
    case WIFI_CIPHER_TYPE_WEP104:
        return "WIFI_CIPHER_TYPE_WEP104";
    case WIFI_CIPHER_TYPE_TKIP:
        return "WIFI_CIPHER_TYPE_TKIP";
    case WIFI_CIPHER_TYPE_CCMP:
        return "WIFI_CIPHER_TYPE_CCMP";
    case WIFI_CIPHER_TYPE_TKIP_CCMP:
        return "WIFI_CIPHER_TYPE_TKIP_CCMP";
    case WIFI_CIPHER_TYPE_AES_CMAC128:
        return "WIFI_CIPHER_TYPE_AES_CMAC128";
    case WIFI_CIPHER_TYPE_SMS4:
        return "WIFI_CIPHER_TYPE_SMS4";
    case WIFI_CIPHER_TYPE_GCMP:
        return "WIFI_CIPHER_TYPE_GCMP";
    case WIFI_CIPHER_TYPE_GCMP256:
        return "WIFI_CIPHER_TYPE_GCMP256";
    default:
        return "WIFI_CIPHER_TYPE_UNKNOWN";
    }
}

//function to print the cipher type
static const char *get_cipher_type_str(int group_cipher)
{
    switch (group_cipher) {
    case WIFI_CIPHER_TYPE_NONE:
        return "WIFI_CIPHER_TYPE_NONE";
    case WIFI_CIPHER_TYPE_WEP40:
        return "WIFI_CIPHER_TYPE_WEP40";
    case WIFI_CIPHER_TYPE_WEP104:
        return "WIFI_CIPHER_TYPE_WEP104";
    case WIFI_CIPHER_TYPE_TKIP:
        return "WIFI_CIPHER_TYPE_TKIP";
    case WIFI_CIPHER_TYPE_CCMP:
        return "WIFI_CIPHER_TYPE_CCMP";
    case WIFI_CIPHER_TYPE_TKIP_CCMP:
        return "WIFI_CIPHER_TYPE_TKIP_CCMP";
    case WIFI_CIPHER_TYPE_SMS4:
        return "WIFI_CIPHER_TYPE_SMS4";
    case WIFI_CIPHER_TYPE_GCMP:
        return "WIFI_CIPHER_TYPE_GCMP";
    case WIFI_CIPHER_TYPE_GCMP256:
        return "WIFI_CIPHER_TYPE_GCMP256";
    default:
        return "WIFI_CIPHER_TYPE_UNKNOWN";
    }
}

//function to print bssid
static const char *get_bssid_str(uint8_t bssid[6]) {
    static char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             bssid[0], bssid[1], bssid[2],
             bssid[3], bssid[4], bssid[5]);
    return mac;
}

static const char *get_antenna_str(int ant) {
    switch (ant) {
    case WIFI_ANT_ANT0:
        return "wifi antenna 0";
    case WIFI_ANT_ANT1:
        return "wifi antenna 1";
    case WIFI_ANT_MAX:
        return "wifi antenna invalid";
    default:
        return "wifi antenna unknown";
    }
}

// Function to print physical layer capabilities: how data are transmitted
// MIMO: multiple antennas / simultaneous data streams
// OFDM: channel divided into parallel subcarriers for data transmission
// OFDMA: multiple users share subcarriers simultaneously (Wi-Fi 6)
static sta_info_strings_t *get_phy_info(wifi_ap_record_t record) {
    static sta_info_strings_t out;
    out.count = 3;

    // init lines
    for (int i = 0; i < out.count; i++)
        out.lines[i][0] = '\0';

    strcat(out.lines[0], "802.11: ");
    strcat(out.lines[1], "FTM: ");
    strcat(out.lines[2], "WPS: ");

    if (record.phy_11b) strcat(out.lines[0], "b/");
    if (record.phy_11g) strcat(out.lines[0], "g/");
    if (record.phy_11n) strcat(out.lines[0], "n/");
    if (record.phy_lr)  strcat(out.lines[0], "lr/");
    if (record.phy_11a) strcat(out.lines[0], "a/");
    if (record.phy_11ac) strcat(out.lines[0], "ac/");
    if (record.phy_11ax) strcat(out.lines[0], "ax/");

    strcat(out.lines[1], record.ftm_responder ? "responder/" : "");
    strcat(out.lines[1], record.ftm_initiator ? "initiator" : "");

    strcat(out.lines[2], record.wps ? "Yes" : "No");

    return &out;
}

//function to print country channels & max power alllowed, policy
static sta_info_strings_t *get_country_info(wifi_country_t country) {
    static sta_info_strings_t out;
    out.count = 0;

    // init lines
    for (int i = 0; i < 5; i++)
        out.lines[i][0] = '\0';

    // copy country code
    char cc[3];
    memcpy(cc, country.cc, 2);
    cc[2] = '\0';

    if (strcmp(cc, "") != 0) {
        out.count = 5;

        snprintf(out.lines[0], sizeof(out.lines[0]), "Country code: %s", cc);
        snprintf(out.lines[1], sizeof(out.lines[1]), "Start channel: %d", country.schan);
        snprintf(out.lines[2], sizeof(out.lines[2]), "Number of channels: %d", country.nchan);
        snprintf(out.lines[3], sizeof(out.lines[3]), "Max TX power: %d dBm", country.max_tx_power);

        const char *policy;
        switch(country.policy) {
            case WIFI_COUNTRY_POLICY_AUTO:
                policy = "auto"; break;
            case WIFI_COUNTRY_POLICY_MANUAL:
                policy = "manual"; break;
            default:
                policy = "unknown"; break;
        }
        snprintf(out.lines[4], sizeof(out.lines[4]), "Policy: %s", policy);
    } else {
        out.count = 1;
        strcpy(out.lines[0], "Country info: empty");
    }

    return &out;
}

//function to print he info on wifi 6 AP
static sta_info_strings_t *get_he_info(wifi_he_ap_info_t he) {
    static sta_info_strings_t out;
    out.count = 4;

    // init lines
    for (int i = 0; i < 4; i++)
        out.lines[i][0] = '\0';

    snprintf(out.lines[0], sizeof(out.lines[0]), "HE BSS color: %d", he.bss_color);
    snprintf(out.lines[1], sizeof(out.lines[1]),
             "Partial BSS color: %s",
             he.partial_bss_color ? "client doesn't see all frames" : "client receives everything");
    snprintf(out.lines[2], sizeof(out.lines[2]),
             "BSS color: %s",
             he.bss_color_disabled ? "disabled" : "enabled");
    snprintf(out.lines[3], sizeof(out.lines[3]), "BSSID index: %d", he.bssid_index);

    return &out;
}

// Function to print Wi-Fi channel bandwidth
static const char *get_bandwidth_str(int bandwidth) {

    switch(bandwidth) {

        // 20 MHz channel: HT20 (802.11n) or generic 20 MHz
        case WIFI_BW20: // same value as WIFI_BW20
            return "20 MHz (HT20)";

        // 40 MHz channel: HT40 (802.11n) or generic 40 MHz
        case WIFI_BW40: // same value as WIFI_BW40
            return "40 MHz (HT40)";
        
        //wifi 5-6 : below

        // 80 MHz channel
        case WIFI_BW80:
            return "80 MHz";

        // 160 MHz channel
        case WIFI_BW160:
            return "160 MHz";

        // 80+80 MHz channel (non-contiguous)
        case WIFI_BW80_BW80:
            return "80+80 MHz (non-contiguous)";

        // Default case if unknown
        default:
            return "unknown";
    }
}

// Function to print VHT center channel frequencies from AP record
static sta_info_strings_t *get_vht_channels_info(wifi_ap_record_t ap_info) {
    static sta_info_strings_t out;
    out.count = 0;

    // init
    for (int i = 0; i < 2; i++)
        out.lines[i][0] = '\0';

    int bandwidth = ap_info.bandwidth;

    if (bandwidth == WIFI_BW80 || bandwidth == WIFI_BW160 || bandwidth == WIFI_BW80_BW80) {
        snprintf(out.lines[out.count], sizeof(out.lines[0]),
                 "Primary center freq: %d", ap_info.vht_ch_freq1);
        out.count++;
    }

    if (bandwidth == WIFI_BW80_BW80) {
        snprintf(out.lines[out.count], sizeof(out.lines[0]),
                 "Secondary center freq: %d", ap_info.vht_ch_freq2);
        out.count++;
    }

    if (out.count == 0) {
        strcpy(out.lines[0], "No VHT channel info");
        out.count = 1;
    }

    return &out;
}

//function to print scan parameters
static sta_info_strings_t *get_scan_params_info(wifi_scan_default_params_t params) {
    static sta_info_strings_t out;
    out.count = 0;

    // init lines
    for (int i = 0; i < 3; i++)
        out.lines[i][0] = '\0';

    snprintf(out.lines[out.count], sizeof(out.lines[0]),
             "Active scan time: %" PRIu32 "-%" PRIu32 " ms",
             params.scan_time.active.min,
             params.scan_time.active.max);
    out.count++;

    snprintf(out.lines[out.count], sizeof(out.lines[0]),
             "Passive scan time: %" PRIu32 " ms",
             params.scan_time.passive);
    out.count++;

    snprintf(out.lines[out.count], sizeof(out.lines[0]),
             "Time between channels: %d ms",
             params.home_chan_dwell_time);
    out.count++;

    return &out;
}

// Function to print Wi-Fi mode of the ESP32
static const char *get_wifi_mode_str(int mode) {
    switch(mode) {
        case WIFI_MODE_NULL:
            return "null (disabled)";
        case WIFI_MODE_STA:
            return "station (STA)";
        case WIFI_MODE_AP:
            return "soft-AP (AP)";
        case WIFI_MODE_APSTA:
            return "station + soft-AP (APSTA)";
        case WIFI_MODE_NAN:
            return "NAN (Neighbor Awareness Networking)";
        case WIFI_MODE_MAX:
            return "max (placeholder)";
        default:
            return "unknown";
    }
}

// Function to print Wi-Fi power save type
static const char *get_ps_str(int ps) {
    switch(ps) {
        case WIFI_PS_NONE:
            return "NONE (Wi-Fi always active)";
        case WIFI_PS_MIN_MODEM:
            return "MIN_MODEM (wake up every DTIM)";
        case WIFI_PS_MAX_MODEM:
            return "MAX_MODEM (wake up per listen_interval)";
        default: {
            static char buf[32];
            snprintf(buf, sizeof(buf), "UNKNOWN (%d)", ps);
            return buf;
        }
    }
}

//function to print protocols : use macro bitmasks
static sta_info_strings_t *get_protocols_info(wifi_protocols_t protocols) {
    static sta_info_strings_t out;
    out.count = 0;

    // init
    for (int i = 0; i < 4; i++) 
        out.lines[i][0] = '\0';

    if (protocols.ghz_2g) {
        strcat(out.lines[out.count], "2.4 GHz: ");
        if (protocols.ghz_2g & WIFI_PROTOCOL_LR)    strcat(out.lines[out.count], "LR ");
        if (protocols.ghz_2g & WIFI_PROTOCOL_11B)   strcat(out.lines[out.count], "11b ");
        if (protocols.ghz_2g & WIFI_PROTOCOL_11G)   strcat(out.lines[out.count], "11g ");
        if (protocols.ghz_2g & WIFI_PROTOCOL_11N)   strcat(out.lines[out.count], "11n ");
        if (protocols.ghz_2g & WIFI_PROTOCOL_11AX)  strcat(out.lines[out.count], "11ax ");
        out.count++;
    }

    if (protocols.ghz_5g) {
        strcat(out.lines[out.count], "5 GHz: ");
        if (protocols.ghz_5g & WIFI_PROTOCOL_11A)   strcat(out.lines[out.count], "11a ");
        if (protocols.ghz_5g & WIFI_PROTOCOL_11N)   strcat(out.lines[out.count], "11n ");
        if (protocols.ghz_5g & WIFI_PROTOCOL_11AC)  strcat(out.lines[out.count], "11ac ");
        if (protocols.ghz_5g & WIFI_PROTOCOL_11AX)  strcat(out.lines[out.count], "11ax ");
        out.count++;
    }

    if (out.count == 0) {
        strcpy(out.lines[0], "No protocol enabled");
        out.count = 1;
    }

    return &out;
}

// Function to print general promiscuous packet filters (non-CTRL)
static sta_info_strings_t *get_promiscuous_filter_info(wifi_promiscuous_filter_t filter) {
    static sta_info_strings_t out;
    out.count = 0;

    // init lines
    for (int i = 0; i < 4; i++)
        out.lines[i][0] = '\0';

    // line 0 = raw mask
    snprintf(out.lines[0], sizeof(out.lines[0]), "Mask: 0x%08lX", filter.filter_mask);
    out.count = 1;

    // line 1 = captured types
    if (filter.filter_mask == WIFI_PROMIS_FILTER_MASK_ALL) {
        strcpy(out.lines[1], "All packet types");
        out.count = 2;
    } else {
        if (filter.filter_mask & WIFI_PROMIS_FILTER_MASK_MGMT)       strcat(out.lines[1], "MGMT/");
        if (filter.filter_mask & WIFI_PROMIS_FILTER_MASK_CTRL)       strcat(out.lines[1], "CTRL/");
        if (filter.filter_mask & WIFI_PROMIS_FILTER_MASK_DATA)       strcat(out.lines[1], "DATA/");
        if (filter.filter_mask & WIFI_PROMIS_FILTER_MASK_MISC)       strcat(out.lines[1], "MISC/");
        if (filter.filter_mask & WIFI_PROMIS_FILTER_MASK_DATA_MPDU)  strcat(out.lines[1], "MPDU/");
        if (filter.filter_mask & WIFI_PROMIS_FILTER_MASK_DATA_AMPDU) strcat(out.lines[1], "AMPDU/");
        if (filter.filter_mask & WIFI_PROMIS_FILTER_MASK_FCSFAIL)    strcat(out.lines[1], "FCSFAIL/");

        // remove the last '/'
        size_t len = strlen(out.lines[1]);
        if (len > 0 && out.lines[1][len-1] == '/') out.lines[1][len-1] = '\0';

        out.count = 2;
    }

    return &out;
}

// Function to print control frame filters in promiscuous mode
static sta_info_strings_t *get_promiscuous_ctrl_filter_info(wifi_promiscuous_filter_t filter) {
    static sta_info_strings_t out;
    out.count = 0;

    // init lines
    for (int i = 0; i < 4; i++)
        out.lines[i][0] = '\0';

    // line 0 = raw mask
    snprintf(out.lines[0], sizeof(out.lines[0]), "CTRL Mask: 0x%08lX", filter.filter_mask);
    out.count = 1;

    // line 1 = captured types
    if (filter.filter_mask == WIFI_PROMIS_CTRL_FILTER_MASK_ALL) {
        strcpy(out.lines[1], "All control packets");
        out.count = 2;
    } else {
        if (filter.filter_mask & WIFI_PROMIS_CTRL_FILTER_MASK_WRAPPER)    strcat(out.lines[1], "WRAPPER/");
        if (filter.filter_mask & WIFI_PROMIS_CTRL_FILTER_MASK_BAR)        strcat(out.lines[1], "BAR/");
        if (filter.filter_mask & WIFI_PROMIS_CTRL_FILTER_MASK_BA)         strcat(out.lines[1], "BA/");
        if (filter.filter_mask & WIFI_PROMIS_CTRL_FILTER_MASK_PSPOLL)     strcat(out.lines[1], "PSPOLL/");
        if (filter.filter_mask & WIFI_PROMIS_CTRL_FILTER_MASK_RTS)        strcat(out.lines[1], "RTS/");
        if (filter.filter_mask & WIFI_PROMIS_CTRL_FILTER_MASK_CTS)        strcat(out.lines[1], "CTS/");
        if (filter.filter_mask & WIFI_PROMIS_CTRL_FILTER_MASK_ACK)        strcat(out.lines[1], "ACK/");
        if (filter.filter_mask & WIFI_PROMIS_CTRL_FILTER_MASK_CFEND)      strcat(out.lines[1], "CFEND/");
        if (filter.filter_mask & WIFI_PROMIS_CTRL_FILTER_MASK_CFENDACK)   strcat(out.lines[1], "CFENDACK/");

        // remove the last '/'
        size_t len = strlen(out.lines[1]);
        if (len > 0 && out.lines[1][len-1] == '/') out.lines[1][len-1] = '\0';

        out.count = 2;
    }

    return &out;
}

//function to print phy mode used between ESP & AP
static const char *get_phy_str(wifi_phy_mode_t mode)
{
    switch (mode) {
        case WIFI_PHY_MODE_LR:
            return "Low Rate (LR)";
        case WIFI_PHY_MODE_11B:
            return "802.11b";
        case WIFI_PHY_MODE_11G:
            return "802.11g";
        case WIFI_PHY_MODE_11A:
            return "802.11a (5 GHz)";
        case WIFI_PHY_MODE_HT20:
            return "802.11n HT20 (20 MHz)";
        case WIFI_PHY_MODE_HT40:
            return "802.11n HT40 (40 MHz)";
        case WIFI_PHY_MODE_VHT20:
            return "802.11ac VHT20 (20 MHz)";
        case WIFI_PHY_MODE_HE20:
            return "802.11ax HE20 (20 MHz)";
        default: {
            static char buf[32];
            snprintf(buf, sizeof(buf), "Unknown (%d)", mode);
            return buf;
        }
    }
}

static const char *get_scan_method_str(wifi_scan_method_t scan) {
    switch (scan)
    {
    case WIFI_FAST_SCAN:
        return "fast";
    case WIFI_ALL_CHANNEL_SCAN:
        return "all channels";
    default:
        return "unknown";
    }
}

static const char *get_sort_method_str(wifi_sort_method_t sort) {
    switch (sort)
    {
    case WIFI_CONNECT_AP_BY_SIGNAL:
        return "Sort APs by RSSI";
    case WIFI_CONNECT_AP_BY_SECURITY:
        return "Sort APs by security mode";
    default:
        return "Sort method unknown";
    }
}

static sta_info_strings_t *get_scan_threshold_info(wifi_scan_threshold_t threshold) {
    static sta_info_strings_t out;
    out.count = 0;

    // init lines
    for (int i = 0; i < 4; i++)
        out.lines[i][0] = '\0';

    snprintf(out.lines[0], sizeof(out.lines[0]), "Min RSSI: %d dBm", threshold.rssi);
    snprintf(out.lines[1], sizeof(out.lines[1]), "Weakest auth mode: %s", get_authmode_str(threshold.authmode));
    snprintf(out.lines[2], sizeof(out.lines[2]), "5G RSSI adjustment: %d dBm", threshold.rssi_5g_adjustment);

    out.count = 3;
    return &out;
}

static const char *get_sae_pk_str(wifi_sae_pk_mode_t pk) {
    switch (pk)
    {
    case WPA3_SAE_PK_MODE_AUTOMATIC:
        return "WPA3 SAE PK auto";
    case WPA3_SAE_PK_MODE_ONLY:
        return "WPA3 SAE PK only";
    case WPA3_SAE_PK_MODE_DISABLED:
        return "WPA3 SAE PK disabled";
    default:
        return "WPA3 SAE PK unknown";
    }
}

static const char *get_sae_pwe_str(wifi_sae_pwe_method_t pwe) {
    switch (pwe)
    {
    case WPA3_SAE_PWE_UNSPECIFIED:
        return "WPA3 SAE PWE unspecified";
    case WPA3_SAE_PWE_HUNT_AND_PECK:
        return "WPA3 SAE PWE hunt & peck";
    case WPA3_SAE_PWE_HASH_TO_ELEMENT:
        return "WPA3 SAE PWE hash to element";
    case WPA3_SAE_PWE_BOTH:
        return "WPA3 SAE PWE both";
    default:
        return "WPA3 SAE PWE unknown";
    }
}

static sta_info_strings_t *get_bss_info(wifi_bss_max_idle_config_t bss) {
    static sta_info_strings_t out;
    out.count = 0;

    // init lines
    for (int i = 0; i < 2; i++)
        out.lines[i][0] = '\0';

    snprintf(out.lines[0], sizeof(out.lines[0]), "BSS max idle: %d TUs", bss.period);
    snprintf(out.lines[1], sizeof(out.lines[1]), "Protected keep alive: %s", bss.protected_keep_alive ? "Yes" : "No");

    out.count = 2;
    return &out;
}

//function to print wifi event mask
static sta_info_strings_t *get_wifi_event_mask_info(uint32_t mask) {
    static sta_info_strings_t out;
    out.count = 0;

    // init lines
    for (int i = 0; i < 4; i++)
        out.lines[i][0] = '\0';

    snprintf(out.lines[0], sizeof(out.lines[0]), "ESP wifi event mask: 0x%08" PRIX32, mask);

    if (mask == WIFI_EVENT_MASK_NONE) {
        strcpy(out.lines[1], "No Wi-Fi events are masked");
        out.count = 2;
        return &out;
    }

    if (mask == WIFI_EVENT_MASK_ALL) {
        strcpy(out.lines[1], "All Wi-Fi events are masked");
        out.count = 2;
        return &out;
    }

    int line_idx = 1;
    if (mask & WIFI_EVENT_MASK_AP_PROBEREQRECVED) {
        strcpy(out.lines[line_idx++], "AP_PROBEREQRECVED");
    }

    uint32_t known_mask = WIFI_EVENT_MASK_AP_PROBEREQRECVED;
    uint32_t unknown = mask & ~known_mask;
    if (unknown) {
        snprintf(out.lines[line_idx++], sizeof(out.lines[0]), "Unknown bits: 0x%08" PRIX32, unknown);
    }

    out.count = line_idx;
    return &out;
}

//function to print ESP's AP config
static sta_info_strings_t *get_config_ap_info(wifi_ap_config_t ap) {
    static sta_info_strings_t out;
    out.count = 0;
    for (int i = 0; i < MAX_STR_LINES; i++) out.lines[i][0] = '\0';

    // line 0: SSID and password info
    snprintf(out.lines[out.count++], sizeof(out.lines[0]),
             "SSID: %s | PASS: %s | SSID len: %d",
             ap.ssid, ap.password, ap.ssid_len);

    // line 1: channel, auth mode, hidden
    snprintf(out.lines[out.count++], sizeof(out.lines[0]),
             "CH: %d | Auth: %s | Hidden: %s",
             ap.channel, get_authmode_str(ap.authmode),
             ap.ssid_hidden ? "Yes" : "No");

    // line 2: Max connections, beacon, CSA, DTIM
    snprintf(out.lines[out.count++], sizeof(out.lines[0]),
             "Max conn: %d | Beacon: %d | CSA: %d | DTIM: %d",
             ap.max_connection, ap.beacon_interval, ap.csa_count, ap.dtim_period);

    // line 3: Pairwise cipher and FTM responder
    snprintf(out.lines[out.count++], sizeof(out.lines[0]),
             "Pairwise: %s | FTM: %s",
             get_cipher_pair_str(ap.pairwise_cipher),
             ap.ftm_responder ? "Enabled" : "Disabled");

    // line 4: PMF required / capable
    snprintf(out.lines[out.count++], sizeof(out.lines[0]),
             "PMF Req: %d | PMF Capable: %d",
             ap.pmf_cfg.required, ap.pmf_cfg.capable);

    // line 5: SAE PWE, transition disable, SAE EXT, WPA3 compatibility
    snprintf(out.lines[out.count++], sizeof(out.lines[0]),
             "SAE PWE: %s | Transition disable: %d | SAE EXT: %d | WPA3 compat: %d",
             get_sae_pwe_str(ap.sae_pwe_h2e),
             ap.transition_disable,
             ap.sae_ext,
             ap.wpa3_compatible_mode);
    

    // line 6 & more : BSS max idle
    sta_info_strings_t *info = get_bss_info(ap.bss_max_idle_cfg);
    for (int i = 0; i < info->count; i++) {
        snprintf(out.lines[out.count++], sizeof(out.lines[0]),
             "BSS [%d]: %.100s", i, info->lines[i]);
    }

    // last line : GTK rekey
    snprintf(out.lines[out.count++], sizeof(out.lines[0]),
                "GTK rekey: %d",
             ap.gtk_rekey_interval);

    return &out;
}

//function to print ESP's STA config;
//Config is used by ESP in STA mode to search APs that fits best
static sta_info_strings_t *get_config_sta_info(wifi_sta_config_t sta) {
    static sta_info_strings_t out;
    out.count = 0;
    for (int i=0; i<MAX_STR_LINES; i++) out.lines[i][0] = '\0';

    char h2e[SAE_H2E_IDENTIFIER_LEN + 1];
    memcpy(h2e, sta.sae_h2e_identifier, SAE_H2E_IDENTIFIER_LEN);
    h2e[SAE_H2E_IDENTIFIER_LEN] = '\0';

    snprintf(out.lines[out.count++], sizeof(out.lines[0]),
             "SSID: %s | PASS: %s",
             sta.ssid, sta.password);
    
    snprintf(out.lines[out.count++], sizeof(out.lines[0]),
             "BSSID set: %s | CH: %d",
             sta.bssid_set ? "Yes":"No", sta.channel);
    
    snprintf(out.lines[out.count++], sizeof(out.lines[0]),
             "Scan: %s",
             get_scan_method_str(sta.scan_method));

    snprintf(out.lines[out.count++], sizeof(out.lines[0]),
             "Listen: %d | Sort: %s | PMF Req: %d Cap: %d",
             sta.listen_interval, get_sort_method_str(sta.sort_method),
             sta.pmf_cfg.required, sta.pmf_cfg.capable);

    sta_info_strings_t *info = get_scan_threshold_info(sta.threshold);
    for (int i = 0; i < info->count; i++) {
        snprintf(out.lines[out.count++], sizeof(out.lines[0]),
             "Threshold [%d]: %.100s", i, info->lines[i]);
    }

    snprintf(out.lines[out.count++], sizeof(out.lines[0]),
             "RM: %s | BTM: %s | MBO: %s | FT: %s | OWE: %s | Transition: %s | Disable WPA3 compat: %s",
             sta.rm_enabled?"Yes":"No", sta.btm_enabled?"Yes":"No",
             sta.mbo_enabled?"Yes":"No", sta.ft_enabled?"Yes":"No",
             sta.owe_enabled?"Yes":"No", sta.transition_disable?"Yes":"No",
             sta.disable_wpa3_compatible_mode?"Yes":"No");

    snprintf(out.lines[out.count++], sizeof(out.lines[0]),
             "SAE PWE: %s | SAE PK: %s | Retry: %d | HE DCM: %d TX:%d RX:%d",
             get_sae_pwe_str(sta.sae_pwe_h2e), get_sae_pk_str(sta.sae_pk_mode),
             sta.failure_retry_cnt, sta.he_dcm_set, sta.he_dcm_max_constellation_tx,
             sta.he_dcm_max_constellation_rx);

    snprintf(out.lines[out.count++], sizeof(out.lines[0]),
             "HE MCS9: %d | HE SU BF: %d | HE TRIG SU: %d MU: %d CQI: %d",
             sta.he_mcs9_enabled, sta.he_su_beamformee_disabled,
             sta.he_trig_su_bmforming_feedback_disabled,
             sta.he_trig_mu_bmforming_partial_feedback_disabled,
             sta.he_trig_cqi_feedback_disabled);

    snprintf(out.lines[out.count++], sizeof(out.lines[0]),
             "VHT SU BF: %d MU BF: %d MCS8: %d | H2E ID: %s",
             sta.vht_su_beamformee_disabled, sta.vht_mu_beamformee_disabled,
             sta.vht_mcs8_enabled, h2e);

    return &out;
}

//function to pint sta info connected to ESP in AP mode
static sta_info_strings_t *get_sta_info(wifi_sta_info_t sta) {
    static sta_info_strings_t out;
    out.count = 0;

    // line 0 : BSSID + AID + RSSI
    uint16_t aid;
    esp_wifi_ap_get_sta_aid(sta.mac, &aid);
    snprintf(out.lines[out.count++], sizeof(out.lines[0]),
             "STA: %02X:%02X:%02X:%02X:%02X:%02X | AID:%d | RSSI:%d",
             sta.mac[0], sta.mac[1], sta.mac[2],
             sta.mac[3], sta.mac[4], sta.mac[5],
             aid, sta.rssi);

    // line 1 : phy
    char phy[64] = "";
    if (sta.phy_11b)  strcat(phy, "11b/");
    if (sta.phy_11g)  strcat(phy, "11g/");
    if (sta.phy_11n)  strcat(phy, "11n/");
    if (sta.phy_lr)   strcat(phy, "LR/");
    if (sta.phy_11a)  strcat(phy, "11a/");
    if (sta.phy_11ac) strcat(phy, "11ac/");
    if (sta.phy_11ax) strcat(phy, "11ax/");
    size_t len = strlen(phy);
    if (len > 0 && phy[len-1] == '/') phy[len-1] = '\0';
    snprintf(out.lines[out.count++], sizeof(out.lines[0]), "PHY: %s%s",
             phy, sta.is_mesh_child ? " | Mesh child" : "");

    // line 2 : optionnal info
    snprintf(out.lines[out.count++], sizeof(out.lines[0]),
             "Mesh child: %s", sta.is_mesh_child ? "Yes" : "No");

    return &out;
}

//function to print a record info AP
//authmode : type of connection (password)
//ssid : name of AP
//rssi : power of signal (-30 dbm, good / -90 dbm, bad)
//cipher : Wifi encryption;
//cipher pairwise : encryption AP / client
//cipher group : encryption broadcast / multicast messages
//channel : 2.4GHz; 1 to 13 (1,6,11 most common); 5GHz; more channels
//channel : two networks cannot be on same channel (interference)
//bssid : mac address of AP
//antenna : which antenna used for wifi
//country : rules of country of AP
//phy : physical layer, how data are transmitted
//he : for wifi 6, info as BSS & bssid index of AP
//bandwidth : type of bandwidth used by wifi channel
//vht : for wifi 5/6, center channel frequencies
static void print_record(wifi_ap_record_t ap_info) {
    sta_info_strings_t * info;

    log_mqtt(LOG_INFO, TAG, true, "=================== SSID %s ===============",
        ap_info.ssid);

    log_mqtt(LOG_INFO, TAG, true, "RSSI : %d dBm",
        ap_info.rssi);

    log_mqtt(LOG_INFO, TAG, true, "Authmode : %s",
        get_authmode_str(ap_info.authmode));

    if (ap_info.authmode != WIFI_AUTH_WEP) {
        log_mqtt(LOG_INFO, TAG, true, "Cipher: Pair: %s / Type : %s",
            get_cipher_pair_str(ap_info.pairwise_cipher), get_cipher_type_str(ap_info.group_cipher));
    }

    log_mqtt(LOG_INFO, TAG, true, "Channel : %d",
        ap_info.primary);
    //ESP_LOGI(TAG, "Secondary channel \t\t%d", ap_info.second);

    log_mqtt(LOG_INFO, TAG, true, "bssid : %s",
        get_bssid_str(ap_info.bssid));

    log_mqtt(LOG_INFO, TAG, true, "antenna : %s",
        get_antenna_str(ap_info.ant));

    info = get_country_info(ap_info.country);
    for (int i = 0; i < info->count; i++) {
        log_mqtt(LOG_INFO, TAG, true, "country [%d] : %s",
            i, info->lines[i]);
    }

    info = get_phy_info(ap_info);
    for (int i = 0; i < info->count; i++) {
        log_mqtt(LOG_INFO, TAG, true, "phy [%d] : %s",
            i, info->lines[i]);
    }
    
    if (ap_info.phy_11ax) {
        info = get_he_info(ap_info.he_ap);
        for (int i = 0; i < info->count; i++) {
            log_mqtt(LOG_INFO, TAG, true, "he [%d] : %s",
                i, info->lines[i]);
        }
    }

    log_mqtt(LOG_INFO, TAG, true, "Bandwidth  : %s",
        get_bandwidth_str(ap_info.bandwidth));
    
    info = get_vht_channels_info(ap_info);
    for (int i = 0; i < info->count; i++) {
        log_mqtt(LOG_INFO, TAG, true, "vht [%d] : %s",
            i, info->lines[i]);
    }
    
    log_mqtt(LOG_INFO, TAG, true, "=================== %s end ===============",
        ap_info.ssid);
}

/*
=====================================================================
USEFUL FUNCTIONS
=====================================================================
*/

static void first_scan() {

    esp_err_t err;
    sta_info_strings_t *info;

    log_mqtt(LOG_INFO, TAG, false, "=============== First scan APs ===============");

    wifi_scan_default_params_t params;
    err = esp_wifi_get_scan_parameters(&params);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error on getting scan parameters : %d", err);
    } else {
        info = get_scan_params_info(params);
        for (int i = 0; i < info->count; i++) {
            log_mqtt(LOG_INFO, TAG, true, "scan params [%d] : %s", i, info->lines[i]);
        }
    }

    //init info array
    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    uint16_t ap_count = 0;
    wifi_ap_record_t* ap_info = malloc(sizeof(wifi_ap_record_t) * DEFAULT_SCAN_LIST_SIZE);
    if (!ap_info) {
        log_mqtt(LOG_ERROR, TAG, true, "Failed to allocate memory for AP scan");
        return;
    }
    memset(ap_info, 0, sizeof(wifi_ap_record_t) * DEFAULT_SCAN_LIST_SIZE);

    //launch scan
    err = esp_wifi_scan_start(NULL, true); //blocking
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Scan failed: %s", esp_err_to_name(err));
        return;
    }

    //if fail : esp_wifi_clear_ap_list

    log_mqtt(LOG_INFO, TAG, false, "Max AP number ap_info can hold = %u", number);

    //get num & records of scan
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) getting AP number", esp_err_to_name(err));
        return;
    }
    
    err = esp_wifi_scan_get_ap_records(&number, ap_info);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) getting AP records", esp_err_to_name(err));
        return;
    }

    log_mqtt(LOG_INFO, TAG, true, "Total APs scanned = %u, actual AP number ap_info holds = %u", ap_count, number);

    wifi_network_t *wifi_credentials = NULL;
    //go through each record and compare ssid
    for (int i = 0; i < number; i++) {
        print_record(ap_info[i]);
        for (int j = 0; j < sizeof(known_networks) / sizeof(known_networks[0]); j++) {
            if (strcmp((const char *)ap_info[i].ssid, known_networks[j].ssid) == 0) {
                if (wifi_credentials == NULL) {
                    wifi_credentials = &known_networks[j];
                } else if (known_networks[j].priority < wifi_credentials->priority) {
                    wifi_credentials = &known_networks[j];
                }
            } //else to add : if open network and wifi_credentials null, store open wifi
        }
    }
    free(ap_info);

    if (wifi_credentials == NULL) {
        log_mqtt(LOG_ERROR, TAG, true, "No network found");
        return;
    }

    //config of ESP station
    wifi_config_t wifi_sta_config = {
        .sta = {
            .ssid = {0},
            .password = {0},
            .scan_method = WIFI_ALL_CHANNEL_SCAN
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
            * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
        },
    };

    strncpy((char *)wifi_sta_config.sta.ssid,
        (const char *)wifi_credentials->ssid, sizeof(wifi_sta_config.sta.ssid));

    strncpy((char *)wifi_sta_config.sta.password,
        (const char *)wifi_credentials->password, sizeof(wifi_sta_config.sta.password));

    //Apply station config to ESP
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting wifi config", esp_err_to_name(err));
        return;
    }

    log_mqtt(LOG_INFO, TAG, false, "=============== First Scan End ===============");
}

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
    esp_err_t err;
    //if wifi starting event
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        first_scan();
        err = esp_wifi_connect(); // Start wifi connection : send request to router
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) connecting wifi", esp_err_to_name(err));
        }
    //if wifi disconnect event, trying to reconnect
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        log_mqtt(LOG_INFO, TAG, true, "Disconnected, trying to reconnect..");
        err = esp_wifi_connect();
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) connecting wifi", esp_err_to_name(err));
        }
        //maximum retry?
    //if router assigned an IP address to ESP (triggered by DHCP netif when connection ok by router)
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        //get IP from data : cast
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        //log wifi connected IP
        log_mqtt(LOG_INFO, TAG, true, "Connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));

        // Store IP address as string for later use
        snprintf(s_ip_str, sizeof(s_ip_str),
                 IPSTR, IP2STR(&event->ip_info.ip));

        //update global handler to wifi connected to unlock tasks waiting for wifi
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        log_mqtt(LOG_INFO, TAG, true, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        log_mqtt(LOG_INFO, TAG, true, "station " MACSTR " leave, AID=%d, reason=%d",
                 MAC2STR(event->mac), event->aid, event->reason);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_ASSIGNED_IP_TO_CLIENT) {
        const ip_event_assigned_ip_to_client_t *e = (const ip_event_assigned_ip_to_client_t *)event_data;
        log_mqtt(LOG_INFO, TAG, true, "Assigned IP to client: " IPSTR ", MAC=" MACSTR ", hostname='%s'",
                 IP2STR(&e->ip), MAC2STR(e->mac), e->hostname);
    }
}

/* Initialize simpel soft AP config netif*/
esp_netif_t *wifi_init_softap_netif(void)
{
    //create netif TCP/IP for AP mode
    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap();

    //config : ssid, length, password, max connection, authmode
    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = ESP_SSID,
            .ssid_len = strlen(ESP_SSID),
            .password = ESP_PASS,
            .max_connection = ESP_MAX_CONNECTION,
            .authmode = WIFI_AUTH_WPA2_PSK
        },
    };

    //apply config AP to ESP
    esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting wifi config", esp_err_to_name(err));
        return esp_netif_ap;
    }

    log_mqtt(LOG_INFO, TAG, true, "wifi_init_softap finished. SSID:%s password:%s",
             ESP_SSID, ESP_PASS);

    return esp_netif_ap;
}

//Set DNS address of ESP AP to transmit the one from ESP STA to clients connected to ESP AP
//DNS : Domain Name System; Converts domain name to IP; Without DNS, use only IP;
//NAT : Network Address Translation; Allows private IPs to access Internet with only one public IP;
//NAPT : Handle several clients at same time using TCP/UDP ports
//DHCP : Dynamic Host Configuration Protocol
//DHCP : Assign auto an IP, Gateway, DNS to clients connecting to ESP AP
static void softap_set_dns_addr(esp_netif_t *esp_netif_ap, esp_netif_t *esp_netif_sta)
{
    esp_netif_dns_info_t dns;

    //getting dns info of ESP STA mode (MAIN DNS)
    esp_err_t err = esp_netif_get_dns_info(esp_netif_sta, ESP_NETIF_DNS_MAIN, &dns);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) getting DNS info", esp_err_to_name(err));
        return;
    }

    //Option for DHCP server of AP; offers DNS to clients when they ask for an IP
    uint8_t dhcps_offer_option = DHCPS_OFFER_DNS;

    //Stops DHCP server to apply a new config
    err = esp_netif_dhcps_stop(esp_netif_ap);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) stopping DHCP", esp_err_to_name(err));
        return;
    }

    //Config DHCP of AP to offer DNS to clients
    //when a client connects, this will tell him which DNS address to use
    err = esp_netif_dhcps_option(esp_netif_ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
        &dhcps_offer_option, sizeof(dhcps_offer_option));
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) getting DHCP option", esp_err_to_name(err));
        err = esp_netif_dhcps_start(esp_netif_ap);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) starting DHCP", esp_err_to_name(err));
        }
        return;
    }

    //Set DNS got from STA to AP
    err = esp_netif_set_dns_info(esp_netif_ap, ESP_NETIF_DNS_MAIN, &dns);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting DNS info", esp_err_to_name(err));
        err = esp_netif_dhcps_start(esp_netif_ap);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error (%s) starting DHCP", esp_err_to_name(err));
        }
        return;
    }

    //Restart DHCP server with new config
    err = esp_netif_dhcps_start(esp_netif_ap);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) starting DHCP", esp_err_to_name(err));
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
    esp_err_t err;

    //Create global handler events for wifi
    s_wifi_event_group = xEventGroupCreate();

    //Initialize netif TCP/IP
    err = esp_netif_init();
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) init netif", esp_err_to_name(err));
        return;
    }
    
    //Create system event loop
    err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) creating event loop", esp_err_to_name(err));
        return;
    }

    //Load default config and initialize driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) allocating wifi resources", esp_err_to_name(err));
        return;
    }

    //register handler for all wifi events
    esp_event_handler_instance_t instance_any_id;
    //register handler for IP event (GOT_IP)
    esp_event_handler_instance_t instance_got_ip;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, &instance_any_id);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) register any handler", esp_err_to_name(err));
        return;
    }
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler, NULL, &instance_got_ip);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) register IP handler", esp_err_to_name(err));
        return;
    }

    //Configure esp in station mode
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting wifi mode", esp_err_to_name(err));
        return;
    }
    
    //esp_netif_t *esp_netif_sta = esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_sta();

    //start wifi : triggers WIFI_EVENT_STA_START handler
    err = esp_wifi_start();
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) starting wifi", esp_err_to_name(err));
        return;
    }

    //debug
    log_mqtt(LOG_INFO, TAG, false, "Connection to ...");

    //Waiting for connection until WIFI_CONNECTED_BIT is activated (if init in main, it will wait..)
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        log_mqtt(LOG_INFO, TAG, true, "Connected to AP");
    } else {
        log_mqtt(LOG_ERROR, TAG, true, "UNEXPECTED EVENT");
        return;
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
void wifi_init_ap(void)
{
    esp_err_t err;

    //Initialize netif TCP/IP
    err = esp_netif_init();
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) init netif", esp_err_to_name(err));
        return;
    }
    
    //Create system event loop
    err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) creating event loop", esp_err_to_name(err));
        return;
    }

    //Load default config and initialize driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) allocating wifi resources", esp_err_to_name(err));
        return;
    }

    //register handler for all wifi events
    esp_event_handler_instance_t instance_any_id;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, &instance_any_id);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) register any handler", esp_err_to_name(err));
        return;
    }

    //Configure esp in AP mode
    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting wifi mode", esp_err_to_name(err));
        return;
    }

    //esp_netif_t * netif_ap = wifi_init_softap_netif();
    esp_netif_t *netif_ap = wifi_init_softap_netif();

    //start wifi : triggers WIFI_EVENT_STA_START handler
    err = esp_wifi_start();
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) starting wifi", esp_err_to_name(err));
        return;
    }

    /*wifi has to be started*/
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(netif_ap, &ip_info);

    snprintf(s_ip_str, sizeof(s_ip_str),
                 IPSTR, IP2STR(&ip_info.ip));

    //debug
    log_mqtt(LOG_INFO, TAG, true, "Wifi Soft-AP initialized; SSID : %s, PASS : %s, IP : " IPSTR,
        ESP_SSID, ESP_PASS, s_ip_str);
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
void wifi_init_apsta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    esp_err_t err;

    //Initialize netif TCP/IP
    err = esp_netif_init();
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) init netif", esp_err_to_name(err));
        return;
    }
    
    //Create system event loop
    err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) creating event loop", esp_err_to_name(err));
        return;
    }

    //Load default config and initialize driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) allocating wifi resources", esp_err_to_name(err));
        return;
    }

    //register handler for all wifi events
    err = esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID,
                    &wifi_event_handler,
                    NULL,
                    NULL);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) register any handler", esp_err_to_name(err));
        return;
    }
    err = esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_STA_GOT_IP,
                    &wifi_event_handler,
                    NULL,
                    NULL);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) register got IP handler", esp_err_to_name(err));
        return;
    }
    err = esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_ASSIGNED_IP_TO_CLIENT,
                    &wifi_event_handler,
                    NULL,
                    NULL);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) register IP assigned handler", esp_err_to_name(err));
        return;
    }

    //Configure esp in APSTA mode
    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting wifi mode", esp_err_to_name(err));
        return;
    }

     /* Initialize AP */
    esp_netif_t *esp_netif_ap = wifi_init_softap_netif();

    /* Initialize STA */
    esp_netif_t *esp_netif_sta = esp_netif_create_default_wifi_sta();

    //start wifi : triggers WIFI_EVENT_STA_START handler
    err = esp_wifi_start();
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) starting wifi", esp_err_to_name(err));
        return;
    }

    //Waiting for connection until WIFI_CONNECTED_BIT is activated (if init in main, it will wait..)
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned,
     * hence we can test which event actually happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        log_mqtt(LOG_INFO, TAG, true, "connected to ap");
        //get dns from sta mode ESP & config it for ESP AP's clients
        //"transmit dns internet from WAN to Clients"
        softap_set_dns_addr(esp_netif_ap, esp_netif_sta);
    } else {
        log_mqtt(LOG_ERROR, TAG, true, "UNEXPECTED EVENT");
        return;
    }

    /* Set sta as the default interface */
    //every Internet request will go through ESP STA mode (not AP)
    err = esp_netif_set_default_netif(esp_netif_sta);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) setting default netif", esp_err_to_name(err));
        return;
    }

    /* Enable napt on the AP netif */
    //transform private IPs of clients from ESP AP to public IP of ESP STA (NAT)
    if (esp_netif_napt_enable(esp_netif_ap) != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "NAPT not enabled on the netif: %p", esp_netif_ap);
    }
}

/**
 * Function to get the IP if someone wants to
 */
const char* wifi_get_ip(void)
{
    return s_ip_str;
}

bool is_scanning() {
    return scanning;
}

void wifi_scan_task(void *pvParameter) {

    sta_info_strings_t *info;

    log_mqtt(LOG_INFO, TAG, true, "=============== Scanning APs ===============");

    wifi_scan_default_params_t params;
    esp_err_t err = esp_wifi_get_scan_parameters(&params);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error on getting scan parameters : %d", err);
    } else {
        info = get_scan_params_info(params);
        for (int i = 0; i < info->count; i++) {
            log_mqtt(LOG_INFO, TAG, true, "scan params [%d] : %s", i, info->lines[i]);
        }
    }

    // init info array
    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
    uint16_t ap_count = 0;
    memset(ap_info, 0, sizeof(ap_info));

    // launch scan
    err = esp_wifi_scan_start(NULL, true); // blocking
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Scan failed: %d", err);
        return;
    }

    // if fail : esp_wifi_clear_ap_list

    log_mqtt(LOG_INFO, TAG, true, "Max AP number ap_info can hold = %u",
        number);

    //get num & records of scan
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) getting AP number", esp_err_to_name(err));
        return;
    }
    
    err = esp_wifi_scan_get_ap_records(&number, ap_info);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error (%s) getting AP records", esp_err_to_name(err));
        return;
    }
    
    log_mqtt(LOG_INFO, TAG, true, "Total APs scanned = %u, actual AP number ap_info holds = %u",
        ap_count, number);

    // go through each record and print authmode, ssid, rssi, cipher, channel..
    for (int i = 0; i < number; i++) {
        print_record(ap_info[i]);
    }
    scanning = false;
    vTaskDelete(NULL);
}

void wifi_scan_aps() {

    if (!scanning) {
        scanning = true;

        xTaskCreate(&wifi_scan_task, "wifi_scan_task", 4096, NULL, 5, NULL);

    } else {
        log_mqtt(LOG_WARN, TAG, true, "Already scanning Wifi");
    }
}

void get_ap_info() {

    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error on getting current AP info : %d", err);
    } else {
        print_record(ap_info);
    }

}

void wifi_scan_esp() {

    // Launching scan
    log_mqtt(LOG_INFO, TAG, true, "=============== Getting ESP wifi info ===============");

    // get current wifi mode
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error on getting wifi mode %d", err);
    } else {
        log_mqtt(LOG_INFO, TAG, true, "Wifi mode : %s", get_wifi_mode_str(mode));
    }

    // get power saving type
    wifi_ps_type_t type;
    err = esp_wifi_get_ps(&type);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error on getting wifi power save type %d", err);
    } else {
        log_mqtt(LOG_INFO, TAG, true, "Power save : %s", get_ps_str(type));
    }

    wifi_protocols_t protocols;
    // get protocols for STA
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        err = esp_wifi_get_protocols(WIFI_IF_STA, &protocols);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error on getting wifi STA protocol %d", err);
        } else {
            log_mqtt(LOG_INFO, TAG, true, "ESP protocols for STA");
            sta_info_strings_t *info = get_protocols_info(protocols);
            for (int i = 0; i < info->count; i++) {
                log_mqtt(LOG_INFO, TAG, true, "Protocols [%d] : %s",
                    i, info->lines[i]);
            }
        }
    }

    // get protocols for AP
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        err = esp_wifi_get_protocols(WIFI_IF_AP, &protocols);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error on getting wifi AP protocol %d", err);
        } else {
            log_mqtt(LOG_INFO, TAG, true, "ESP protocols for AP");
            sta_info_strings_t *info = get_protocols_info(protocols);
            for (int i = 0; i < info->count; i++) {
                log_mqtt(LOG_INFO, TAG, true, "Protocols [%d] : %s",
                    i, info->lines[i]);
            }
        }
    }

    wifi_bandwidth_t bw;
    // get bandwidth for STA
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        err = esp_wifi_get_bandwidth(WIFI_IF_STA, &bw);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error on getting wifi STA bandwidth %d", err);
        } else {
            log_mqtt(LOG_INFO, TAG, true, "ESP STA Bandwidth : %s", get_bandwidth_str(bw));
        }
    }

    // get bandwidth for AP
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        err = esp_wifi_get_bandwidth(WIFI_IF_AP, &bw);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error on getting wifi AP bandwidth %d", err);
        } else {
            log_mqtt(LOG_INFO, TAG, true, "ESP AP Bandwidth : %s", get_bandwidth_str(bw));
        }
    }

    // get channels
    uint8_t primary;
    wifi_second_chan_t second;
    err = esp_wifi_get_channel(&primary, &second);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error on getting channel %d", err);
    } else {
        log_mqtt(LOG_INFO, TAG, true, "Wifi ESP primary channel %d", primary);
        // log_mqtt(LOG_INFO, TAG, true, "Secondary channel %d", second);
    }

    // get country info
    wifi_country_t country;
    err = esp_wifi_get_country(&country);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error on getting country %d", err);
    } else {
        sta_info_strings_t *info = get_country_info(country);
        for (int i = 0; i < info->count; i++) {
            log_mqtt(LOG_INFO, TAG, true, "Country [%d] : %s", i, info->lines[i]);
        }
    }

    // get MAC addresses
    uint8_t mac[6];

    // STA MAC
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        err = esp_wifi_get_mac(WIFI_IF_STA, mac);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error on getting mac STA %d", err);
        } else {
            log_mqtt(LOG_INFO, TAG, true, "MAC STA : %s", get_bssid_str(mac));
        }
    }

    // AP MAC
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        err = esp_wifi_get_mac(WIFI_IF_AP, mac);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error on getting mac AP %d", err);
        } else {
            log_mqtt(LOG_INFO, TAG, true, "MAC AP : %s", get_bssid_str(mac));
        }
    }

    // get promiscuous mode
    bool en;
    err = esp_wifi_get_promiscuous(&en);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error on getting promiscuous %d", err);
    } else {
        log_mqtt(LOG_INFO, TAG, true, "Promiscuous mode : %s", en ? "enabled" : "disabled");
    }

    // if promiscuous, get filter info
    if (en) {
        wifi_promiscuous_filter_t filter;
        err = esp_wifi_get_promiscuous_filter(&filter);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error on getting promiscuous filter %d", err);
        } else {
            sta_info_strings_t *info = get_promiscuous_filter_info(filter);
            for (int i = 0; i < info->count; i++) {
                log_mqtt(LOG_INFO, TAG, true, "Promiscuous filter [%d]: %s", i, info->lines[i]);
            }

            if (filter.filter_mask & WIFI_PROMIS_FILTER_MASK_CTRL) {
                wifi_promiscuous_filter_t ctrl_filter;
                err = esp_wifi_get_promiscuous_ctrl_filter(&ctrl_filter);
                if (err != ESP_OK) {
                    log_mqtt(LOG_ERROR, TAG, true, "Error on getting promiscuous control filter %d", err);
                } else {
                    sta_info_strings_t *ctrl_info = get_promiscuous_ctrl_filter_info(ctrl_filter);
                    for (int i = 0; i < ctrl_info->count; i++) {
                        log_mqtt(LOG_INFO, TAG, true, "Promiscuous CTRL filter [%d]: %s", i, ctrl_info->lines[i]);
                    }
                }
            }
        }
    }

    // get STA config
    wifi_config_t conf;
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        err = esp_wifi_get_config(WIFI_IF_STA, &conf);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error on getting config STA %d", err);
        } else {
            sta_info_strings_t *info = get_config_sta_info(conf.sta);
            for (int i = 0; i < info->count; i++) {
                log_mqtt(LOG_INFO, TAG, true, "STA config [%d]: %s", i, info->lines[i]);
            }
        }
    }

    // get AP config
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        err = esp_wifi_get_config(WIFI_IF_AP, &conf);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error on getting config AP %d", err);
        } else {
            sta_info_strings_t *info = get_config_ap_info(conf.ap);
            for (int i = 0; i < info->count; i++) {
                log_mqtt(LOG_INFO, TAG, true, "AP config [%d]: %s", i, info->lines[i]);
            }
        }
    }

    // get list of connected STAs if in AP mode
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        wifi_sta_list_t stas;
        err = esp_wifi_ap_get_sta_list(&stas);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error on getting STA list %d", err);
        } else {
            for (int i = 0; i < stas.num; i++) {
                sta_info_strings_t *info = get_sta_info(stas.sta[i]);
                for (int j = 0; j < info->count; j++) {
                    log_mqtt(LOG_INFO, TAG, true, "STA [%d] info [%d]: %s", i, j, info->lines[j]);
                }
            }
        }
    }

    // if STA, print connected AP info
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        get_ap_info();
    }

    // get max tx power
    int8_t power;
    err = esp_wifi_get_max_tx_power(&power);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error on max tx power %d", err);
    } else {
        log_mqtt(LOG_INFO, TAG, true, "ESP max TX power : %.2f dBm", power * 0.25f);
    }

    // get wifi event mask
    uint32_t mask;
    err = esp_wifi_get_event_mask(&mask);
    if (err != ESP_OK) {
        log_mqtt(LOG_ERROR, TAG, true, "Error on getting wifi event mask %d", err);
    } else {
        sta_info_strings_t *info = get_wifi_event_mask_info(mask);
        for (int i = 0; i < info->count; i++) {
            log_mqtt(LOG_INFO, TAG, true, "Event mask [%d]: %s", i, info->lines[i]);
        }
    }

    // get TSF time: Timing Synchronization Function
    // To sync devices on a same network
    log_mqtt(LOG_INFO, TAG, true, "TSF time STA: %" PRId64 " us", esp_wifi_get_tsf_time(WIFI_IF_STA));
    log_mqtt(LOG_INFO, TAG, true, "TSF time AP: %" PRId64 " us", esp_wifi_get_tsf_time(WIFI_IF_AP));

    // get inactive times
    uint16_t sec;
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        err = esp_wifi_get_inactive_time(WIFI_IF_STA, &sec);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error on getting inactive time STA %d", err);
        } else {
            log_mqtt(LOG_INFO, TAG, true, "Inactive time for ESP STA: %d s", sec);
        }
    }

    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        err = esp_wifi_get_inactive_time(WIFI_IF_AP, &sec);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error on getting inactive time AP %d", err);
        } else {
            log_mqtt(LOG_INFO, TAG, true, "Inactive time for ESP AP: %d s", sec);
        }
    }

    // get aid assigned to ESP if STA
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        uint16_t aid;
        err = esp_wifi_sta_get_aid(&aid);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error on getting ESP aid %d", err);
        } else {
            log_mqtt(LOG_INFO, TAG, true, "AP's association id of ESP: %d", aid);
        }
    }

    // get negotiated phymode
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        wifi_phy_mode_t phymode;
        err = esp_wifi_sta_get_negotiated_phymode(&phymode);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error on getting used phymode STA %d", err);
        } else {
            log_mqtt(LOG_INFO, TAG, true, "Negotiated phymode : %s", get_phy_str(phymode));
        }
    }

    // get RSSI if STA
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        int rssi;
        err = esp_wifi_sta_get_rssi(&rssi);
        if (err != ESP_OK) {
            log_mqtt(LOG_ERROR, TAG, true, "Error on getting RSSI %d", err);
        } else {
            log_mqtt(LOG_INFO, TAG, true, "RSSI : %d dBm", rssi);
        }
    }

    // print all statistics (low level)
    // esp_wifi_statis_dump(WIFI_STATIS_ALL);

    log_mqtt(LOG_INFO, TAG, true, "=============== Getting ESP wifi info End ===============");
}

//ap mode :

//kick one client from aid
//esp_wifi_deauth_sta

//set mac address
//esp_wifi_set_mac

//esp_wifi_ap_get_sta_list
//esp_wifi_ap_get_sta_aid

//advanced functions :

//ftm : determine position with wifi; see example

//force not getting wifi sleep mode
//esp_wifi_force_wakeup_acquire
//esp_wifi_force_wakeup_release

//redundant info
//esp_wifi_set_country_code; better
//esp_wifi_set_country; fill all values

//low level : 
//esp_wifi_config_80211_tx_rate
//esp_wifi_config_80211_tx
//csi config
//esp_wifi_action_tx_req

//disable frame protection
//esp_wifi_disable_pmf_config

//auto channel selection in AP mode
//esp_wifi_set_dynamic_cs

//stay on a channel a certain time
//esp_wifi_remain_on_channel


//esp_eap_client? (wpa_supplicant)