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

 //scan AP max number
#define DEFAULT_SCAN_LIST_SIZE 16

//wifi credentials
#define WIFI_SSID "freebox_isidor"
#define WIFI_PASS "casanova1664"

#define ESP_SSID "big_esp"
#define ESP_PASS "espdegigachad"
#define ESP_MAX_CONNECTION 4

/*DHCP server option*/
#define DHCPS_OFFER_DNS             0x02

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

//function to print the wifi authentication mode
static void print_auth_mode(int authmode)
{
    switch (authmode) {
    case WIFI_AUTH_OPEN:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_OPEN");
        break;
    case WIFI_AUTH_OWE:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_OWE");
        break;
    case WIFI_AUTH_WEP:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WEP");
        break;
    case WIFI_AUTH_WPA_PSK:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA_PSK");
        break;
    case WIFI_AUTH_WPA2_PSK:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA2_PSK");
        break;
    case WIFI_AUTH_WPA_WPA2_PSK:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA_WPA2_PSK");
        break;
    case WIFI_AUTH_ENTERPRISE:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_ENTERPRISE");
        break;
    case WIFI_AUTH_WPA3_PSK:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA3_PSK");
        break;
    case WIFI_AUTH_WPA2_WPA3_PSK:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA2_WPA3_PSK");
        break;
    case WIFI_AUTH_WPA3_ENTERPRISE:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA3_ENTERPRISE");
        break;
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA2_WPA3_ENTERPRISE");
        break;
    case WIFI_AUTH_WPA3_ENT_192:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA3_ENT_192");
        break;
    default:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_UNKNOWN");
        break;
    }
}

static void print_cipher_pair(int pairwise) {
    switch (pairwise) {
    case WIFI_CIPHER_TYPE_NONE:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_NONE");
        break;
    case WIFI_CIPHER_TYPE_WEP40:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_WEP40");
        break;
    case WIFI_CIPHER_TYPE_WEP104:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_WEP104");
        break;
    case WIFI_CIPHER_TYPE_TKIP:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_TKIP");
        break;
    case WIFI_CIPHER_TYPE_CCMP:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_CCMP");
        break;
    case WIFI_CIPHER_TYPE_TKIP_CCMP:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_TKIP_CCMP");
        break;
    case WIFI_CIPHER_TYPE_AES_CMAC128:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_AES_CMAC128");
        break;
    case WIFI_CIPHER_TYPE_SMS4:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_SMS4");
        break;
    case WIFI_CIPHER_TYPE_GCMP:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_GCMP");
        break;
    case WIFI_CIPHER_TYPE_GCMP256:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_GCMP256");
        break;
    default:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_UNKNOWN");
        break;
    }
}

//function to print the cipher type (what's this??)
static void print_cipher_type(int pairwise_cipher, int group_cipher)
{
    print_cipher_pair(pairwise_cipher);

    switch (group_cipher) {
    case WIFI_CIPHER_TYPE_NONE:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_NONE");
        break;
    case WIFI_CIPHER_TYPE_WEP40:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_WEP40");
        break;
    case WIFI_CIPHER_TYPE_WEP104:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_WEP104");
        break;
    case WIFI_CIPHER_TYPE_TKIP:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_TKIP");
        break;
    case WIFI_CIPHER_TYPE_CCMP:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_CCMP");
        break;
    case WIFI_CIPHER_TYPE_TKIP_CCMP:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_TKIP_CCMP");
        break;
    case WIFI_CIPHER_TYPE_SMS4:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_SMS4");
        break;
    case WIFI_CIPHER_TYPE_GCMP:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_GCMP");
        break;
    case WIFI_CIPHER_TYPE_GCMP256:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_GCMP256");
        break;
    default:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_UNKNOWN");
        break;
    }
}

//function to print bssid
static void print_bssid(uint8_t bssid[6]) {
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             bssid[0], bssid[1], bssid[2],
             bssid[3], bssid[4], bssid[5]);
    ESP_LOGI(TAG, "BSSID : \t\t%s", mac);
}

static void print_antenna(int ant) {
    switch (ant) {
    case  WIFI_ANT_ANT0:
        ESP_LOGI(TAG, "Antenna used \twifi antenna 0");
        break;
    case WIFI_ANT_ANT1:
        ESP_LOGI(TAG, "Antenna used \twifi antenna 1");
        break;
    case WIFI_ANT_MAX:
        ESP_LOGI(TAG, "Antenna used \twifi antenna invalid");
        break;
    default:
        ESP_LOGI(TAG, "Antenna used \twifi antenna unknown");
        break;
    }
}

// Function to print physical layer capabilities: how data are transmitted
// MIMO: multiple antennas / simultaneous data streams
// OFDM: channel divided into parallel subcarriers for data transmission
// OFDMA: multiple users share subcarriers simultaneously (Wi-Fi 6)
static void print_phy(wifi_ap_record_t record) {
    if (record.phy_11b) {
        // 2.4 GHz, old standard, maximum data rate 1-11 Mbps
        ESP_LOGI(TAG, "Supports 802.11b");
    }
    if (record.phy_11g) {
        // 2.4 GHz, newer than 11b, up to 54 Mbps
        ESP_LOGI(TAG, "Supports 802.11g");
    }
    if (record.phy_11n) {
        // 2.4 & 5 GHz, MIMO support, OFDM, up to 600 Mbps
        ESP_LOGI(TAG, "Supports 802.11n");
    }
    if (record.phy_lr) {
        // Low Rate mode, used for IoT or long-range communication at low speeds
        ESP_LOGI(TAG, "Supports low rate");
    }
    if (record.phy_11a) {
        // 5 GHz only, up to 54 Mbps, older standard
        ESP_LOGI(TAG, "Supports 802.11a");
    }
    if (record.phy_11ac) {
        // 5 GHz, MIMO enhanced, OFDM, up to 1Gbps, Wi-Fi 5 standard
        ESP_LOGI(TAG, "Supports 802.11ac");
    }
    if (record.phy_11ax) {
        // 2.4 & 5 GHz, OFDMA, MIMO, up to 10Gbps, Wi-Fi 6 standard
        ESP_LOGI(TAG, "Supports 802.11ax");
    }
    if (record.wps) {
        // Wi-Fi Protected Setup supported, easy connection via button or PIN
        ESP_LOGI(TAG, "Supports WPS");
    }
    if (record.ftm_responder) {
        // FTM (Fine Timing Measurement) supported in responder mode for precise localization
        ESP_LOGI(TAG, "Supports FTM responder mode");
    }
    if (record.ftm_initiator) {
        // FTM supported in initiator mode, AP can initiate distance measurements
        ESP_LOGI(TAG, "Supports FTM initiator mode");
    }
}

//function to print country channels & max power alllowed, policy
static void print_country(wifi_country_t country) {

    //copy string to add \0
    char cc[3];
    memcpy(cc, country.cc, 2);
    cc[2] = '\0';

    if (strcmp(cc,"") != 0) {
        ESP_LOGI(TAG, "Country code : \t%s", cc);
        ESP_LOGI(TAG, "Country start channel (2.4GHz) : \t%d", country.schan);
        ESP_LOGI(TAG, "Country number of channels (2.4GHz) : \t%d", country.nchan);
        ESP_LOGI(TAG, "Country max tx power : \t\t%d dBm", country.max_tx_power);
        switch(country.policy) {
            case WIFI_COUNTRY_POLICY_AUTO :
                ESP_LOGI(TAG, "Country policy : \tauto");
                break;
            case WIFI_COUNTRY_POLICY_MANUAL :
                ESP_LOGI(TAG, "Country policy : \tmanual");
                break;
            default:
                ESP_LOGI(TAG, "Country policy : \tunknown");
                break;
        }
    } else {
        ESP_LOGI(TAG, "Country info : \tempty");
    }
}

//function to print he info on wifi 6 AP
static void print_he(wifi_he_ap_info_t he) {

    // identifies the AP in dense networks with number 0-63 assigned to AP
    ESP_LOGI(TAG, "HE BSS color : \t%d", he.bss_color);

    // for partial interference management :  client only sees part of frames
    if (he.partial_bss_color) {
        ESP_LOGI(TAG, "Client doesn't see all frames");
    } else {
        ESP_LOGI(TAG, "Client receive everything");
    }

    // BSS feature used by this AP?
    if (he.bss_color_disabled) {
        ESP_LOGI(TAG, "BSS color disabled");
    } else {
        ESP_LOGI(TAG, "BSS color enabled");
    }

    // identifies the AP’s BSSID index (for non-transmitted or virtual BSS)
    ESP_LOGI(TAG, "AP's BSSID index : \t%d", he.bssid_index);
}

// Function to print Wi-Fi channel bandwidth
static void print_bandwidth(int bandwidth) {

    switch(bandwidth) {

        // 20 MHz channel: HT20 (802.11n) or generic 20 MHz
        case WIFI_BW_HT20: // same value as WIFI_BW20
            ESP_LOGI(TAG, "Bandwidth \t20 MHz (HT20)");
            break;

        // 40 MHz channel: HT40 (802.11n) or generic 40 MHz
        case WIFI_BW_HT40: // same value as WIFI_BW40
            ESP_LOGI(TAG, "Bandwidth \t40 MHz (HT40)");
            break;
        
        //wifi 5-6 : below

        // 80 MHz channel
        case WIFI_BW80:
            ESP_LOGI(TAG, "Bandwidth \t80 MHz");
            break;

        // 160 MHz channel
        case WIFI_BW160:
            ESP_LOGI(TAG, "Bandwidth \t160 MHz");
            break;

        // 80+80 MHz channel (non-contiguous)
        case WIFI_BW80_BW80:
            ESP_LOGI(TAG, "Bandwidth \t80+80 MHz (non-contiguous)");
            break;

        // Default case if unknown
        default:
            ESP_LOGI(TAG, "Bandwidth \tunknown");
            break;
    }
}

// Function to print VHT center channel frequencies from AP record
static void print_vht_channels(wifi_ap_record_t ap_info) {
    int bandwidth = ap_info.bandwidth;

    // vht_ch_freq1: center frequency of the primary channel segment
    // For 80 MHz or 160 MHz bandwidth, this is the main center channel
    // For 80+80 MHz, this is the center of the lower frequency segment
    if (bandwidth == WIFI_BW80 || bandwidth == WIFI_BW160 || bandwidth == WIFI_BW80_BW80) {
        ESP_LOGI(TAG, "VHT primary center channel frequency: \t%d", ap_info.vht_ch_freq1);
    }

    // vht_ch_freq2: used only for 80+80 MHz bandwidth
    // Transmits the center channel frequency of the second (upper) segment
    if (bandwidth == WIFI_BW80_BW80) {
        ESP_LOGI(TAG, "VHT secondary center channel frequency: \t%d", ap_info.vht_ch_freq2);
    }
}

//function to print scan parameters
static void print_scan_params(wifi_scan_default_params_t params) {
    //send probes by wifi and APs respond
    ESP_LOGI(TAG, "Time range per channel : %d ms to %d ms",
        params.scan_time.active.max, params.scan_time.active.min);
    //esp only listen beacons by APs
    ESP_LOGI(TAG, "Passive time per channel : %d ms", params.scan_time.passive);
    //time between each channel
    ESP_LOGI(TAG, "Time between channels : %d ms", params.home_chan_dwell_time);
}

// Function to print Wi-Fi mode of the ESP32
static void print_wifi_mode(int mode) {
    switch(mode) {
        case WIFI_MODE_NULL:
            // Null mode : Wi-Fi disabled
            ESP_LOGI(TAG, "Wi-Fi mode : \tnull (disabled)");
            break;

        case WIFI_MODE_STA:
            // Station mode : ESP32 connects to an AP
            ESP_LOGI(TAG, "Wi-Fi mode : \tstation (STA)");
            break;

        case WIFI_MODE_AP:
            // Soft-AP mode : ESP32 acts as an access point
            ESP_LOGI(TAG, "Wi-Fi mode : \tsoft-AP (AP)");
            break;

        case WIFI_MODE_APSTA:
            // Station + Soft-AP : ESP32 can connect to AP and provide its own AP
            ESP_LOGI(TAG, "Wi-Fi mode : \tstation + soft-AP (APSTA)");
            break;

        case WIFI_MODE_NAN:
            // NAN mode : Neighbor Awareness Networking (Wi-Fi P2P)
            ESP_LOGI(TAG, "Wi-Fi mode : \tNAN (Neighbor Awareness Networking)");
            break;

        case WIFI_MODE_MAX:
            // Max value placeholder, not used
            ESP_LOGI(TAG, "Wi-Fi mode : \tmax (placeholder)");
            break;

        default:
            // Unknown mode
            ESP_LOGI(TAG, "Wi-Fi mode : \tunknown");
            break;
    }
}

// Function to print Wi-Fi power save type
static void print_ps(int ps) {
    switch(ps) {
        case WIFI_PS_NONE:
            // No power save : Wi-Fi always active, higher power consumption
            ESP_LOGI(TAG, "Power save mode : NONE (Wi-Fi always active)");
            break;

        case WIFI_PS_MIN_MODEM:
            // Minimum modem power saving
            // ESP32 wakes up to receive beacons every DTIM period
            ESP_LOGI(TAG, "Power save mode : MIN_MODEM (wake up every DTIM)");
            break;

        case WIFI_PS_MAX_MODEM:
            // Maximum modem power saving
            // ESP32 wakes up to receive beacons based on listen_interval
            ESP_LOGI(TAG, "Power save mode : MAX_MODEM (wake up per listen_interval)");
            break;

        default:
            ESP_LOGI(TAG, "Power save mode : UNKNOWN (%d)", ps);
            break;
    }
}

//function to print protocols : use macro bitmasks
static void print_protocols(wifi_protocols_t protocols)
{

    if (protocols.ghz_2g) {
        ESP_LOGI(TAG, "  2.4 GHz:");
        if (protocols.ghz_2g & WIFI_PROTOCOL_LR)
            ESP_LOGI(TAG, "    - Long Range (LR)");
        if (protocols.ghz_2g & WIFI_PROTOCOL_11B)
            ESP_LOGI(TAG, "    - 802.11b");
        if (protocols.ghz_2g & WIFI_PROTOCOL_11G)
            ESP_LOGI(TAG, "    - 802.11g");
        if (protocols.ghz_2g & WIFI_PROTOCOL_11N)
            ESP_LOGI(TAG, "    - 802.11n");
        if (protocols.ghz_2g & WIFI_PROTOCOL_11AX)
            ESP_LOGI(TAG, "    - 802.11ax (Wi-Fi 6)");
    }

    if (protocols.ghz_5g) {
        ESP_LOGI(TAG, "  5 GHz:");
        if (protocols.ghz_5g & WIFI_PROTOCOL_11A)
            ESP_LOGI(TAG, "    - 802.11a");
        if (protocols.ghz_5g & WIFI_PROTOCOL_11N)
            ESP_LOGI(TAG, "    - 802.11n");
        if (protocols.ghz_5g & WIFI_PROTOCOL_11AC)
            ESP_LOGI(TAG, "    - 802.11ac (Wi-Fi 5)");
        if (protocols.ghz_5g & WIFI_PROTOCOL_11AX)
            ESP_LOGI(TAG, "    - 802.11ax (Wi-Fi 6)");
    }

    if (protocols.ghz_2g == 0 && protocols.ghz_5g == 0) {
        ESP_LOGI(TAG, "  No protocol enabled");
    }
}

// Function to print general promiscuous packet filters (non-CTRL)
static void print_promiscuous_filter(wifi_promiscuous_filter_t filter)
{
    // Print the raw mask value for debugging
    ESP_LOGI(TAG, "Promiscuous filter mask: 0x%08lX", filter.filter_mask);

    if (filter.filter_mask == WIFI_PROMIS_FILTER_MASK_ALL) {
        ESP_LOGI(TAG, "  All packet types are captured");
        return;
    }

    // Management frames (beacons, association requests, etc.)
    if (filter.filter_mask & WIFI_PROMIS_FILTER_MASK_MGMT)
        ESP_LOGI(TAG, "  - Management frames (MGMT)");

    // Control frames (ACK, RTS, CTS, etc.) -- usually filtered separately
    if (filter.filter_mask & WIFI_PROMIS_FILTER_MASK_CTRL)
        ESP_LOGI(TAG, "  - Control frames (CTRL)");

    // Data frames (payload data between stations/APs)
    if (filter.filter_mask & WIFI_PROMIS_FILTER_MASK_DATA)
        ESP_LOGI(TAG, "  - Data frames (DATA)");

    // Miscellaneous frames (like beacon reports, vendor-specific)
    if (filter.filter_mask & WIFI_PROMIS_FILTER_MASK_MISC)
        ESP_LOGI(TAG, "  - Miscellaneous frames (MISC)");

    // Only specific MPDU data frames
    if (filter.filter_mask & WIFI_PROMIS_FILTER_MASK_DATA_MPDU)
        ESP_LOGI(TAG, "  - Data MPDU frames");

    // Aggregated AMPDU frames (multiple frames sent together)
    if (filter.filter_mask & WIFI_PROMIS_FILTER_MASK_DATA_AMPDU)
        ESP_LOGI(TAG, "  - Data AMPDU frames");

    // Frames with FCS errors (frame check sequence failed)
    if (filter.filter_mask & WIFI_PROMIS_FILTER_MASK_FCSFAIL)
        ESP_LOGI(TAG, "  - Frames with FCS errors");
}


// Function to print control frame filters in promiscuous mode
static void print_promiscuous_ctrl_filter(wifi_promiscuous_filter_t filter)
{
    // Print the raw mask value (useful for debugging)
    ESP_LOGI(TAG, "Promiscuous CTRL filter mask: 0x%08lX", filter.filter_mask);

    // If all control frame types are filtered
    if (filter.filter_mask == WIFI_PROMIS_CTRL_FILTER_MASK_ALL) {
        ESP_LOGI(TAG, "  All control packets"); // All control frames are captured
        return;
    }

    // Each bit represents a specific subtype of control frame
    // Wrapper frame type (encapsulates other frames)
    if (filter.filter_mask & WIFI_PROMIS_CTRL_FILTER_MASK_WRAPPER)
        ESP_LOGI(TAG, "  - Control Wrapper"); 

    // Request to acknowledge a block of frames
    if (filter.filter_mask & WIFI_PROMIS_CTRL_FILTER_MASK_BAR)
        ESP_LOGI(TAG, "  - Block Ack Request (BAR)"); 

    // Acknowledgment of a block of frames
    if (filter.filter_mask & WIFI_PROMIS_CTRL_FILTER_MASK_BA)
        ESP_LOGI(TAG, "  - Block Ack (BA)"); 

    // Power-Save poll frame, wakes up station in power-save mode
    if (filter.filter_mask & WIFI_PROMIS_CTRL_FILTER_MASK_PSPOLL)
        ESP_LOGI(TAG, "  - PS-Poll"); 

    // Request to Send (asking for permission to send data)
    if (filter.filter_mask & WIFI_PROMIS_CTRL_FILTER_MASK_RTS)
        ESP_LOGI(TAG, "  - RTS"); 

    // Clear to Send (permission to transmit data)
    if (filter.filter_mask & WIFI_PROMIS_CTRL_FILTER_MASK_CTS)
        ESP_LOGI(TAG, "  - CTS"); 

    // Acknowledgment of a previously received frame
    if (filter.filter_mask & WIFI_PROMIS_CTRL_FILTER_MASK_ACK)
        ESP_LOGI(TAG, "  - ACK"); 

    // Marks the end of a contention-free sequence
    if (filter.filter_mask & WIFI_PROMIS_CTRL_FILTER_MASK_CFEND)
        ESP_LOGI(TAG, "  - CF-END"); 
        
    // End of sequence + associated acknowledgment
    if (filter.filter_mask & WIFI_PROMIS_CTRL_FILTER_MASK_CFENDACK)
        ESP_LOGI(TAG, "  - CF-END + CF-ACK"); 

}

//function to print phy mode used between ESP & AP
static void print_phy_mode(wifi_phy_mode_t mode)
{
    switch (mode) {
        case WIFI_PHY_MODE_LR:
            ESP_LOGI(TAG, "PHY mode used : \tLow Rate (LR)");
            break;

        case WIFI_PHY_MODE_11B:
            ESP_LOGI(TAG, "PHY mode used : \t802.11b");
            break;

        case WIFI_PHY_MODE_11G:
            ESP_LOGI(TAG, "PHY mode used : \t802.11g");
            break;

        case WIFI_PHY_MODE_11A:
            ESP_LOGI(TAG, "PHY mode used : \t802.11a (5 GHz)");
            break;

        case WIFI_PHY_MODE_HT20:
            ESP_LOGI(TAG, "PHY mode used : \t802.11n HT20 (20 MHz)");
            break;

        case WIFI_PHY_MODE_HT40:
            ESP_LOGI(TAG, "PHY mode used : \t802.11n HT40 (40 MHz)");
            break;

        case WIFI_PHY_MODE_VHT20:
            ESP_LOGI(TAG, "PHY mode used : \t802.11ac VHT20 (20 MHz)");
            break;

        case WIFI_PHY_MODE_HE20:
            ESP_LOGI(TAG, "PHY mode used : \t802.11ax HE20 (20 MHz)");
            break;

        default:
            ESP_LOGW(TAG, "PHY mode used : \tUnknown (%d)", mode);
            break;
    }
}

static void print_scan_method(wifi_scan_method_t scan) {
    switch (scan)
    {
    case WIFI_FAST_SCAN:
        ESP_LOGI(TAG, "Scan method : fast ");
        break;
    case WIFI_ALL_CHANNEL_SCAN:
        ESP_LOGI(TAG, "Scan method : all channels ");
        break;
    default:
        ESP_LOGI(TAG, "Scan method : unknown");
        break;
    }
}

static void print_sort_method(wifi_sort_method_t sort) {
    switch (sort)
    {
    case WIFI_CONNECT_AP_BY_SIGNAL:
        ESP_LOGI(TAG, "Sort APs by RSSI");
        break;
    case WIFI_CONNECT_AP_BY_SECURITY:
        ESP_LOGI(TAG, "Sort APs by security mode");
        break;
    default:
        ESP_LOGI(TAG, "Sort method unknown");
        break;
    }
}

static void print_scan_threshold(wifi_scan_threshold_t threshold) {
    ESP_LOGI(TAG, "Minimum RSSI in Fast scan : %d dBm", threshold.rssi);
    ESP_LOGI(TAG, "Weakest authmode to accept in fast scan : ");
    print_auth_mode(threshold.authmode);
    ESP_LOGI(TAG, "RSSI 5G AP (priority) : %d dBm", threshold.rssi_5g_adjustment);    
}

static void print_sae_pk(wifi_sae_pk_mode_t pk) {
    switch (pk)
    {
    case WPA3_SAE_PK_MODE_AUTOMATIC:
        ESP_LOGI(TAG, "WPA3 SAE PK auto");
        break;
    case WPA3_SAE_PK_MODE_ONLY:
        ESP_LOGI(TAG, "WPA3 SAE PK only");
        break;
    case WPA3_SAE_PK_MODE_DISABLED:
        ESP_LOGI(TAG, "WPA3 SAE PK disabled");
        break;
    default:
        ESP_LOGI(TAG, "WPA3 SAE PK unknown");
        break;
    }
}

static void print_sae_pwe(wifi_sae_pwe_method_t pwe) {
    switch (pwe)
    {
    case WPA3_SAE_PWE_UNSPECIFIED:
        ESP_LOGI(TAG, "WPA3 SAE PWE unspecified");
        break;
    case WPA3_SAE_PWE_HUNT_AND_PECK:
        ESP_LOGI(TAG, "WPA3 SAE PWE hunt & peck");
        break;
    case WPA3_SAE_PWE_HASH_TO_ELEMENT:
        ESP_LOGI(TAG, "WPA3 SAE PWE hash to element");
        break;
    case WPA3_SAE_PWE_BOTH:
        ESP_LOGI(TAG, "WPA3 SAE PWE both");
        break;
    default:
        ESP_LOGI(TAG, "WPA3 SAE PWE unknown");
        break;
    }
}

static void print_bss(wifi_bss_max_idle_config_t bss) {
    ESP_LOGI(TAG, "BSS max idle period : %d TUs", bss.period);
    ESP_LOGI(TAG, "Protected keep alive required %s",
        bss.protected_keep_alive ? "Yes" : "No");
}

//function to print wifi event mask
static void print_wifi_event_mask(uint32_t mask)
{
    ESP_LOGI(TAG, "ESP wifi event mask : 0x%08" PRIX32, mask);

    // Special cases
    if (mask == WIFI_EVENT_MASK_NONE) {
        ESP_LOGI(TAG, "  No Wi-Fi events are masked (all events enabled)");
        return;
    }

    if (mask == WIFI_EVENT_MASK_ALL) {
        ESP_LOGI(TAG, "  All Wi-Fi events are masked");
        return;
    }

    // Known maskable events
    if (mask & WIFI_EVENT_MASK_AP_PROBEREQRECVED) {
        ESP_LOGI(TAG, "  - AP probe request received event is masked");
    }

    // Detect unknown / undocumented bits
    uint32_t known_mask =
        WIFI_EVENT_MASK_AP_PROBEREQRECVED;

    uint32_t unknown = mask & ~known_mask;
    if (unknown) {
        ESP_LOGW(TAG, "  - Unknown masked bits: 0x%08" PRIX32, unknown);
    }
}

//function to print ESP's AP config
static void print_config_ap(wifi_ap_config_t ap) {
    ESP_LOGI(TAG, "=== Soft-AP Configuration ===");
    ESP_LOGI(TAG, "SSID           : %s" , ap.ssid);
    ESP_LOGI(TAG, "Password       : %s", ap.password);
    ESP_LOGI(TAG, "SSID length    : %d", ap.ssid_len);
    //channel used by ESP to emit
    ESP_LOGI(TAG, "Channel        : %d", ap.channel);
    
    //type of security authentication
    print_auth_mode(ap.authmode);

    //if SSID visible in wifi scan from clients
    ESP_LOGI(TAG, "SSID hidden    : %s", ap.ssid_hidden ? "Yes" : "No");

    //Maximum number of clients
    ESP_LOGI(TAG, "Max connections: %d", ap.max_connection);

    //Interval between each beacon sent by ESP AP
    ESP_LOGI(TAG, "Beacon interval: %d TU", ap.beacon_interval);

    //Channel switch announcement
    //If ESP AP needs to change channel, this counter shows how many beacons needed before the switch
    ESP_LOGI(TAG, "CSA count      : %d", ap.csa_count);

    //Delivery Traffic Indication Message
    //How many beacons before a client in ps mode receive broadcast messages
    ESP_LOGI(TAG, "DTIM period    : %d", ap.dtim_period);

    //Type of encryption used to secure communication with each client
    print_cipher_pair(ap.pairwise_cipher);

    //Fine Timing Measurement (802.11mc)
    //Useful to measure distance
    ESP_LOGI(TAG, "FTM responder  : %s", ap.ftm_responder ? "Enabled" : "Disabled");
    
    //Protected Management Frames
    ESP_LOGI(TAG, "PMF config     : Required=%d, Capable=%d", 
        ap.pmf_cfg.required, ap.pmf_cfg.capable);

    //SAE (WPA3) parameters
    //H2E : Hash-to-Element; Secure WPA3 handshake
    print_sae_pwe(ap.sae_pwe_h2e);

    //Unallow some transitions between APs or networks
    ESP_LOGI(TAG, "Transition disable: %d", ap.transition_disable);
    //Simultaneous Authentication of Equals – Extended
    //Protocol of authentication used by WPA3 to replace PSK; more robust WPA3 handshake
    //PSK : Pre-shared key; Wifi SSID / Password
    ESP_LOGI(TAG, "SAE EXT        : %d", ap.sae_ext);
    //If true, ESP STA will not make WPA3 connection compatible with WPA2
    ESP_LOGI(TAG, "WPA3 compatible mode: %d", ap.wpa3_compatible_mode);

    //BSS idle timeout
    //How much time for an inactive client associated before being disconnected
    print_bss(ap.bss_max_idle_cfg);

    //Interval of renewal of GTK keys (Group temporal key)
    //WPA2-3 security for broadcast comms
    ESP_LOGI(TAG, "GTK rekey interval: %d sec", ap.gtk_rekey_interval);
}

//function to print ESP's STA config;
//Config is used by ESP in STA mode to search APs that fits best
static void print_config_sta(wifi_sta_config_t sta) {
    ESP_LOGI(TAG, "=== STA Configuration ===");
    ESP_LOGI(TAG, "SSID           : %s", sta.ssid);
    ESP_LOGI(TAG, "Password       : %s", sta.password);
    
    print_scan_method(sta.scan_method);

    //if ESP wants to connect to specific MAC address
    ESP_LOGI(TAG, "BSSID set      : %s", sta.bssid_set ? "Yes" : "No");
    if (sta.bssid_set) {
        print_bssid(sta.bssid);
    }

    //Channel index hint
    ESP_LOGI(TAG, "Channel hint   : %d", sta.channel);

    //Interval in Beacon Periods for STA to listen AP beacons
    ESP_LOGI(TAG, "Listen interval: %d", sta.listen_interval);

    //Sort APs by RSSI or Security
    print_sort_method(sta.sort_method);

    //minimum signal to connect
    print_scan_threshold(sta.threshold);

    //Protected Management Frames
    ESP_LOGI(TAG, "PMF config     : Required=%d, Capable=%d", 
        sta.pmf_cfg.required, sta.pmf_cfg.capable);

    //Radio Measurement 802.11k to collect info on signal and APs
    ESP_LOGI(TAG, "Radio Measurement enabled: %s", sta.rm_enabled ? "Yes" : "No");

    //BSS Transmition management 802.11v
    //Allows to AP to indicate to ESP STA to change AP for better performance
    ESP_LOGI(TAG, "BTM enabled              : %s", sta.btm_enabled ? "Yes" : "No");

    //Multi Band Operation / Opportunistic
    //Allows STA to choose best Channel / Bandwidth auto
    ESP_LOGI(TAG, "MBO enabled              : %s", sta.mbo_enabled ? "Yes" : "No");

    //Fast Transition (802.11r)
    //Fast roaming between APs
    ESP_LOGI(TAG, "FT enabled               : %s", sta.ft_enabled ? "Yes" : "No");

    //Opportunistic Wireless Encryption
    //Open wifi but encryptionned
    ESP_LOGI(TAG, "OWE enabled              : %s", sta.owe_enabled ? "Yes" : "No");

    //Unallow some transitions between APs or networks
    ESP_LOGI(TAG, "Transition disable       : %s", sta.transition_disable ? "Yes" : "No");

    //If true, ESP STA will not make WPA3 connection compatible with WPA2
    ESP_LOGI(TAG, "Disable WPA3 compatible  : %s", sta.disable_wpa3_compatible_mode ? "Yes" : "No");

    //SAE (WPA3) parameters
    //H2E : Hash-to-Element; Secure WPA3 handshake
    print_sae_pwe(sta.sae_pwe_h2e);
    //PK : Public Key; generation mode of keys
    print_sae_pk(sta.sae_pk_mode);

    //Number of retry of STA connection before failure
    ESP_LOGI(TAG, "Failure retry count      : %d", sta.failure_retry_cnt);

    //HE params (wifi 6, 802.11ax)
    //DCM : Dual Carrier Modulation, enhance range
    ESP_LOGI(TAG, "HE DCM set               : %d", sta.he_dcm_set);
    ESP_LOGI(TAG, "HE DCM max constellation TX : %d", sta.he_dcm_max_constellation_tx);
    ESP_LOGI(TAG, "HE DCM max constellation RX : %d", sta.he_dcm_max_constellation_rx);
    //MCS9 : modulation coding scheme, transmission speed
    ESP_LOGI(TAG, "HE MCS9 enabled           : %d", sta.he_mcs9_enabled);
    //Beamforming : optimize signal direction
    ESP_LOGI(TAG, "HE SU beamformee disabled : %d", sta.he_su_beamformee_disabled);
    //TRIG feedback : generates debug on quality
    ESP_LOGI(TAG, "HE TRIG SU feedback disabled : %d", sta.he_trig_su_bmforming_feedback_disabled);
    ESP_LOGI(TAG, "HE TRIG MU partial feedback disabled : %d", sta.he_trig_mu_bmforming_partial_feedback_disabled);
    ESP_LOGI(TAG, "HE TRIG CQI feedback disabled : %d", sta.he_trig_cqi_feedback_disabled);

    //VHT parameters (Wi-Fi 5 / 802.11ac)
    //SU : Single user signal direction optimization
    ESP_LOGI(TAG, "VHT SU beamformee disabled : %d", sta.vht_su_beamformee_disabled);
    //MU : Multi user (MIMO) signal direction optimization
    ESP_LOGI(TAG, "VHT MU beamformee disabled : %d", sta.vht_mu_beamformee_disabled);
    //MCS8 : modulation coding scheme, transmission speed
    ESP_LOGI(TAG, "VHT MCS8 enabled           : %d", sta.vht_mcs8_enabled);

    //print H2E id used by WPA3 SAE (secure handshake between AP / STA)
    char buf[SAE_H2E_IDENTIFIER_LEN + 1];
    memcpy(buf, sta.sae_h2e_identifier, SAE_H2E_IDENTIFIER_LEN);
    buf[SAE_H2E_IDENTIFIER_LEN] = '\0';
    ESP_LOGI(TAG, "SAE H2E identifier : %s", buf);
}

//function to pint sta info connected to ESP in AP mode
static void print_sta_info(wifi_sta_list_t stas) {

    uint16_t aid;
    for (int i=0; i<stas.num; i++) {

        print_bssid(stas.sta[i].mac);

        esp_wifi_ap_get_sta_aid(stas.sta[i].mac, &aid);
        ESP_LOGI(TAG, "STA associated id : %d", aid);

        ESP_LOGI(TAG, "STA average RSSI : %d", stas.sta[i].rssi);
        
        if (stas.sta[i].phy_11b) {
        // 2.4 GHz, old standard, maximum data rate 1-11 Mbps
        ESP_LOGI(TAG, "Supports 802.11b");
        }
        if (stas.sta[i].phy_11g) {
            // 2.4 GHz, newer than 11b, up to 54 Mbps
            ESP_LOGI(TAG, "Supports 802.11g");
        }
        if (stas.sta[i].phy_11n) {
            // 2.4 & 5 GHz, MIMO support, OFDM, up to 600 Mbps
            ESP_LOGI(TAG, "Supports 802.11n");
        }
        if (stas.sta[i].phy_lr) {
            // Low Rate mode, used for IoT or long-range communication at low speeds
            ESP_LOGI(TAG, "Supports low rate");
        }
        if (stas.sta[i].phy_11a) {
            // 5 GHz only, up to 54 Mbps, older standard
            ESP_LOGI(TAG, "Supports 802.11a");
        }
        if (stas.sta[i].phy_11ac) {
            // 5 GHz, MIMO enhanced, OFDM, up to 1Gbps, Wi-Fi 5 standard
            ESP_LOGI(TAG, "Supports 802.11ac");
        }
        if (stas.sta[i].phy_11ax) {
            // 2.4 & 5 GHz, OFDMA, MIMO, up to 10Gbps, Wi-Fi 6 standard
            ESP_LOGI(TAG, "Supports 802.11ax");
        }
        //wifi mesh with esp_mesh; if STA connected to parent node
        if (stas.sta[i].is_mesh_child) {
            ESP_LOGI(TAG, "Is mesh child");
        }
    }

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
    ESP_LOGI(TAG, "=================== SSID %s ===============", ap_info.ssid);
    ESP_LOGI(TAG, "RSSI \t\t%d dBm", ap_info.rssi);
    print_auth_mode(ap_info.authmode);
    if (ap_info.authmode != WIFI_AUTH_WEP) {
        print_cipher_type(ap_info.pairwise_cipher, ap_info.group_cipher);
    }
    ESP_LOGI(TAG, "Channel \t\t%d", ap_info.primary);
    //ESP_LOGI(TAG, "Secondary channel \t\t%d", ap_info.second);
    print_bssid(ap_info.bssid);
    print_antenna(ap_info.ant);
    print_country(ap_info.country);
    print_phy(ap_info);
    if (ap_info.phy_11ax) {
        print_he(ap_info.he_ap);
    }
    print_bandwidth(ap_info.bandwidth);
    print_vht_channels(ap_info);
    ESP_LOGI(TAG, "================================================");
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
    //if wifi starting event
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect(); // Start wifi connection : send request to router
    //if wifi disconnect event, trying to reconnect
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected, trying to reconnect..");
        esp_wifi_connect();
        //maximum retry?
    //if router assigned an IP address to ESP (triggered by DHCP netif when connection ok by router)
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        //get IP from data : cast
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        //log wifi connected IP
        ESP_LOGI(TAG, "Connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));

        // Store IP address as string for later use
        snprintf(s_ip_str, sizeof(s_ip_str),
                 IPSTR, IP2STR(&event->ip_info.ip));

        //update global handler to wifi connected to unlock tasks waiting for wifi
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d, reason=%d",
                 MAC2STR(event->mac), event->aid, event->reason);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_ASSIGNED_IP_TO_CLIENT) {
        const ip_event_assigned_ip_to_client_t *e = (const ip_event_assigned_ip_to_client_t *)event_data;
        ESP_LOGI(TAG, "Assigned IP to client: " IPSTR ", MAC=" MACSTR ", hostname='%s'",
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
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s",
             ESP_SSID, ESP_PASS);

    return esp_netif_ap;
}

/* Initialize wifi station config netif*/
esp_netif_t *wifi_init_sta_netif(void)
{
    //create netif TCP/IP for station mode
    esp_netif_t *esp_netif_sta = esp_netif_create_default_wifi_sta();

    //config of ESP station
    wifi_config_t wifi_sta_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .scan_method = WIFI_ALL_CHANNEL_SCAN
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
            * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
        },
    };

    //Apply station config to ESP
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config) );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    return esp_netif_sta;
}

//Set DNS address of ESP AP to transmit the one from ESP STA to clients connected to ESP AP
//DNS : Domain Name System; Converts domain name to IP; Without DNS, use only IP;
//NAT : Network Address Translation; Allows private IPs to access Internet with only one public IP;
//NAT : Change source IP of request to public IP (here, public IP of ESP STA)
//NAPT : Handle several clients at same time using TCP/UDP ports
//DHCP : Dynamic Host Configuration Protocol
//DHCP : Assign auto an IP, Gateway, DNS to clients connecting to ESP AP
static void softap_set_dns_addr(esp_netif_t *esp_netif_ap, esp_netif_t *esp_netif_sta)
{
    esp_netif_dns_info_t dns;

    //getting dns info of ESP STA mode (MAIN DNS)
    esp_netif_get_dns_info(esp_netif_sta, ESP_NETIF_DNS_MAIN, &dns);

    //Option for DHCP server of AP; offers DNS to clients when they ask for an IP
    uint8_t dhcps_offer_option = DHCPS_OFFER_DNS;

    //Stops DHCP server to apply a new config
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(esp_netif_ap));

    //Config DHCP of AP to offer DNS to clients
    //when a client connects, this will tell him which DNS address to use
    ESP_ERROR_CHECK(esp_netif_dhcps_option(esp_netif_ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
        &dhcps_offer_option, sizeof(dhcps_offer_option)));

    //Set DNS got from STA to AP
    ESP_ERROR_CHECK(esp_netif_set_dns_info(esp_netif_ap, ESP_NETIF_DNS_MAIN, &dns));

    //Restart DHCP server with new config
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(esp_netif_ap));
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
    //Create global handler events for wifi
    s_wifi_event_group = xEventGroupCreate();

    //Initialize netif TCP/IP
    ESP_ERROR_CHECK(esp_netif_init());
    
    //Create system event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    //Load default config and initialize driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    //register handler for all wifi events
    esp_event_handler_instance_t instance_any_id;
    //register handler for IP event (GOT_IP)
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler, NULL, &instance_got_ip);

    //Configure esp in station mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    //esp_netif_t * netif_sta = wifi_init_sta_netif();
    wifi_init_sta_netif();
    
    //start wifi : triggers WIFI_EVENT_STA_START handler
    ESP_ERROR_CHECK(esp_wifi_start());

    //debug
    ESP_LOGI(TAG, "Connection to %s...", WIFI_SSID);

    //Waiting for connection until WIFI_CONNECTED_BIT is activated (if init in main, it will wait..)
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 WIFI_SSID, WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
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

    //Initialize netif TCP/IP
    ESP_ERROR_CHECK(esp_netif_init());
    
    //Create system event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    //Load default config and initialize driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    //register handler for all wifi events
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, &instance_any_id);

    //Configure esp in AP mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    //esp_netif_t * netif_ap = wifi_init_softap_netif();
    wifi_init_softap_netif();

    //start wifi : triggers WIFI_EVENT_STA_START handler
    ESP_ERROR_CHECK(esp_wifi_start());

    //debug
    ESP_LOGI(TAG, "Wifi Soft-AP initialized; SSID : %s, PASS : %s", ESP_SSID, ESP_PASS);
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

    //Initialize netif TCP/IP
    ESP_ERROR_CHECK(esp_netif_init());
    
    //Create system event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    //Load default config and initialize driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    //register handler for all wifi events
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID,
                    &wifi_event_handler,
                    NULL,
                    NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_STA_GOT_IP,
                    &wifi_event_handler,
                    NULL,
                    NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_ASSIGNED_IP_TO_CLIENT,
                    &wifi_event_handler,
                    NULL,
                    NULL));

    //Configure esp in AP mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

     /* Initialize AP */
    esp_netif_t *esp_netif_ap = wifi_init_softap_netif();

    /* Initialize STA */
    esp_netif_t *esp_netif_sta = wifi_init_sta_netif();

    //start wifi : triggers WIFI_EVENT_STA_START handler
    ESP_ERROR_CHECK(esp_wifi_start());

    //Waiting for connection until WIFI_CONNECTED_BIT is activated (if init in main, it will wait..)
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned,
     * hence we can test which event actually happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 WIFI_SSID, WIFI_PASS);
        //get dns from sta mode ESP & config it for ESP AP's clients
        //"transmit dns internet from WAN to Clients"
        softap_set_dns_addr(esp_netif_ap, esp_netif_sta);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
        return;
    }

    /* Set sta as the default interface */
    //every Internet request will go through ESP STA mode (not AP)
    esp_netif_set_default_netif(esp_netif_sta);

    /* Enable napt on the AP netif */
    //transform private IPs of clients from ESP AP to public IP of ESP STA (NAT)
    if (esp_netif_napt_enable(esp_netif_ap) != ESP_OK) {
        ESP_LOGE(TAG, "NAPT not enabled on the netif: %p", esp_netif_ap);
    }
}

/**
 * Function to get the IP if someone wants to
 */
const char* wifi_get_ip(void)
{
    return s_ip_str;
}

void wifi_scan_aps() {

    ESP_LOGI(TAG, "=============== Scanning APs ===============");

    wifi_scan_default_params_t params;
    esp_err_t err = esp_wifi_get_scan_parameters(&params);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error on getting scan parameters : %d", err);
    } else {
        print_scan_params(params);
    }

    //init info array
    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
    uint16_t ap_count = 0;
    memset(ap_info, 0, sizeof(ap_info));

    //launch scan
    err = esp_wifi_scan_start(NULL, true); //blocking
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scan failed: %d", err);
        return;
    }

    //if fail : esp_wifi_clear_ap_list

    ESP_LOGI(TAG, "Max AP number ap_info can hold = %u", number);

    //get num & records of scan
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
    ESP_LOGI(TAG, "Total APs scanned = %u, actual AP number ap_info holds = %u", ap_count, number);

    //go through each record and print authmode, ssid, rssi, cipher, channel..
    for (int i = 0; i < number; i++) {
        print_record(ap_info[i]);
    }
}

void get_ap_info() {

    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,"Error on getting current AP info  : %d", err);
    } else {
        print_record(ap_info);
    }

}

void wifi_scan_esp() {

    ESP_LOGI(TAG, "=============== Getting ESP wifi info ===============");

    //get current wifi mode
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error on getting wifi mode %d", err);
    } else {
        print_wifi_mode(mode);
    }

    //get power saving type
    wifi_ps_type_t type;
    err = esp_wifi_get_ps(&type);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error on getting wifi power save type %d", err);
    } else {
        print_ps(type);
    }

    wifi_protocols_t protocols;
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        //get protocols STA
        err = esp_wifi_get_protocols(WIFI_IF_STA, &protocols);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error on getting wifi STA protocol %d", err);
        } else {
            ESP_LOGI(TAG, "ESP protocols for STA");
            print_protocols(protocols);
        }
    }

    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        //get protocols AP
        err = esp_wifi_get_protocols(WIFI_IF_AP, &protocols);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error on getting wifi AP protocol %d", err);
        } else {
            ESP_LOGI(TAG, "ESP protocols for AP");
            print_protocols(protocols);
        }
    }

    wifi_bandwidth_t bw;
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        //get bandwidth STA
        err = esp_wifi_get_bandwidth(WIFI_IF_STA, &bw);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error on getting wifi STA bandwidth %d", err);
        } else {
            ESP_LOGI(TAG, "ESP bandwidth for STA");
            print_bandwidth(bw);
        }
    }

    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        //get bandwidth AP
        err = esp_wifi_get_bandwidth(WIFI_IF_AP, &bw);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error on getting wifi AP bandwidth %d", err);
        } else {
            ESP_LOGI(TAG, "ESP bandwidth for AP");
            print_bandwidth(bw);
        }
    }

    //get channels
    uint8_t primary;
    wifi_second_chan_t second;
    err = esp_wifi_get_channel(&primary, &second);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error on getting channel %d", err);
    } else {
        ESP_LOGI(TAG, "Wifi ESP primary channel %d", primary);
        //ESP_LOGI(TAG, "Secondary channel %d", ap_info.second);
    }

    //get country
    wifi_country_t country;
    err = esp_wifi_get_country(&country);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error on getting country %d", err);
    } else {
        print_country(country);
    }

    //get mac STA
    uint8_t mac[6];
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        err = esp_wifi_get_mac(WIFI_IF_STA, mac);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error on getting mac STA %d", err);
        } else {
            print_bssid(mac);
        }
    }

    //get mac AP
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        err = esp_wifi_get_mac(WIFI_IF_AP, mac);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error on getting mac AP %d", err);
        } else {
            print_bssid(mac);
        }
    }

    //get promiscuous
    //if enabled, ESP receive every frame, even the ones which are not for it (wireshark)
    bool en;
    err = esp_wifi_get_promiscuous(&en);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error on getting promiscuous %d", err);
    } else {
        ESP_LOGI(TAG, "Promiscuous mode : \t%s", en ? "enabled" : "disabled");
    }

    //if promiscuous, get filter
    if (en) {
        wifi_promiscuous_filter_t filter;
        err = esp_wifi_get_promiscuous_filter(&filter);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error on getting promiscuous filter %d", err);
        } else {
            print_promiscuous_filter(filter);

            // If control frames are enabled, print their subtypes
            if (filter.filter_mask & WIFI_PROMIS_FILTER_MASK_CTRL) {
                wifi_promiscuous_filter_t ctrl_filter;
                err = esp_wifi_get_promiscuous_ctrl_filter(&ctrl_filter);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Error on getting promiscuous control filter %d", err);
                } else {
                    print_promiscuous_ctrl_filter(ctrl_filter);
                }
            }
        }
    }

    //get config STA
    wifi_config_t conf;
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        err = esp_wifi_get_config(WIFI_IF_STA, &conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error on getting config STA %d", err);
        } else {
            print_config_sta(conf.sta);
        }
    }

    //get config AP
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        err = esp_wifi_get_config(WIFI_IF_AP, &conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error on getting config AP %d", err);
        } else {
            print_config_ap(conf.ap);
        }
    }

    //get config NAN

    
    //get sta list / aid associated with ESP AP if set
    wifi_sta_list_t sta;
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        err = esp_wifi_ap_get_sta_list(&sta);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error on getting sta list %d", err);
        } else {
            print_sta_info(sta);
        }
    }

    //if STA, print AP info
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        get_ap_info();
    }

    //get max tx power
    int8_t power;
    err = esp_wifi_get_max_tx_power(&power);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error on max tx power %d", err);
    } else {
        ESP_LOGI(TAG, "ESP max TX power : \t%.2f dBm", power * 0.25f);
    }
    
    //get wifi event mask
    uint32_t mask;
    err = esp_wifi_get_event_mask(&mask);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error on getting wifi event mask %d", err);
    } else {
        print_wifi_event_mask(mask);
    }

    //get TSF time : Timing Synchronization Function;
    //To sync devices on a same network
    ESP_LOGI(TAG, "TSF time STA : \t%" PRId64 " us", esp_wifi_get_tsf_time(WIFI_IF_STA));
    ESP_LOGI(TAG, "TSF time AP : \t%" PRId64 " us", esp_wifi_get_tsf_time(WIFI_IF_AP));

    //get inactive time in seconds if STA : disconnect / deauth from AP after
    uint16_t sec;
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        err = esp_wifi_get_inactive_time(WIFI_IF_STA, &sec);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error on getting inactive time STA %d", err);
        } else {
            ESP_LOGI(TAG, "Inactive time for ESP STA : %d s", sec);
        }
    }

    //get inactive time if AP
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        err = esp_wifi_get_inactive_time(WIFI_IF_AP, &sec);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error on getting inactive time AP %d", err);
        } else {
            ESP_LOGI(TAG, "Inactive time for ESP AP : %d s", sec);
        }
    }

    //get aid assigned to esp by AP if STA
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        uint16_t aid;
        err = esp_wifi_sta_get_aid(&aid);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error on getting ESP aid %d", err);
        } else {
            //if 0, not connected to an AP
            ESP_LOGI(TAG, "AP's association id of ESP: \t%d", aid);
        }
    }

    //get used phymode between ESP / AP
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        wifi_phy_mode_t phymode;
        err = esp_wifi_sta_get_negotiated_phymode(&phymode);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error on getting used phymode STA %d", err);
        } else {
            print_phy_mode(phymode);
        }
    }
    
    //get rssi if STA
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        int rssi;
        err = esp_wifi_sta_get_rssi(&rssi);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error on getting RSSI %d", err);
        } else {
            ESP_LOGI(TAG, "RSSI : \t%d dBm", rssi);
        }
    }

    //print all statistics (low level)
    //esp_wifi_statis_dump(WIFI_STATIS_ALL);

    ESP_LOGI(TAG, "================================================");

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