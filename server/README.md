# Project for ESP32

# Upgrades

- Debug functions for everything
- ifndef for each non-used librairies, edit with KConfig

# Menuconfig

```bash
idf.py menuconfig
```

Compiler options : 
- optimization level, optimize for size (compress)

Partition table : 
- Custom partition table CSV (for OTA & others)
- Serial Flasher config → flash size 4Mb (depending on the chip)

Allow http for ota?

Component config **wifi**
- DNS → Enable DNS server settings with netif
- DNS → Enable DNS fallback (8.8.8.8)
- LWIP → Enable IP forwarding
- LWIP → Enable NAT & NAPT

Component config **websocket**
- HTTP Server → enable websocket support

Component config **udp**
- Default UDP receive mail box size : 32 (increase -> reduce packet loss for udp)

# WS Library

# Wifi Library

https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_wifi.html

with Netif for TCP/IP;

https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_netif.html

with event group for wifi events;

https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos_idf.html#event-group-api

with esp event for event loop;

https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/esp_event.html

WiFi stack:
- esp_wifi (MAC + PHY)
- esp_netif (TCP/IP interface)
- esp_event (central event loop)
- FreeRTOS Event Groups (synchronization)

APSTA mode:
- ESP acts as both WiFi client (STA) and Access Point (AP)
- ESP works as a small router (NAT + DHCP + DNS forwarding)

Menuconfig (APSTA):
- DNS → Enable DNS server settings with netif
- DNS → Enable DNS fallback (8.8.8.8)
- LWIP → Enable IP forwarding
- LWIP → Enable NAT & NAPT

Workflow:
1. Configure WiFi AP + STA
2. STA connects to WAN (router)
3. Retrieve DNS from STA netif
4. Configure DHCP on AP to advertise DNS
5. Enable NAPT to route traffic from AP clients to STA

WiFi concepts:
- WPA2-PSK: classic SSID + password
- WPA3-SAE: modern secure handshake (no offline attacks)
- WPA2/WPA3 Enterprise (802.1X): EAP authentication via RADIUS
- Cipher: encryption algorithm (CCMP, GCMP, etc.)
- BSSID: MAC address of an AP
- RSSI: signal strength (-90 dBm weak → -20 dBm strong)

PHY / Standards (depends on ESP32 variant):
- 2.4 GHz / 5 GHz
- 802.11b/g/n
- 802.11ax (Wi-Fi 6 on supported chips)
- MIMO, OFDM, OFDMA

Wi-Fi 6 (802.11ax):
- HE (High Efficiency)
- BSS Coloring
- OFDMA
- Improved performance in dense networks

Power save:
- Modem sleep min / max

Promiscuous mode:
- Raw WiFi frame capture (Wireshark-like)

FTM (802.11mc):
- Fine Timing Measurement
- Distance estimation using RTT

Security:
- SAE PWE: password-to-element method (H2E recommended)
- SAE PK: public-key validation against rogue APs

ESP AP configuration:
- SSID, Password, Channel
- Cipher, PMF, DTIM
- Beacon interval, CSA count
- WPA2 / WPA3 / SAE

ESP STA configuration:
- SSID / BSSID
- Scan sort / threshold
- PMF, 802.11k/v/r
- OWE, WPA3, SAE PWE / PK
- HE / VHT parameters

Mesh networking:
- Nodes organized in parent / child topology

# OTA Library

Over The Air Updates

OTA HTTPS

Edit partitions to a custom partition in menuconfig

```
# Name, Type, SubType, Offset, Size, Flags
nvs,      data, nvs,       , 0x6000
otadata,  data, ota,       , 0x2000
phy_init, data, phy,       , 0x1000
factory,  app,  factory,   , 1M
ota_0,    app,  ota_0,     , 1M
ota_1,    app,  ota_1,     , 1M
spiffs,   data, spiffs,    , 1M
```

--> ota 0 & 1 & data : for OTA
--> Spiffs : web pages
--> nvs : wifi storage
--> phy_init : for wifi phy
--> factory : for base app

# mDNS

Could be useful for finding devices on local network with dynamic IP. mDNS allows us to communicate via esp-32.local insteand of 192.162.1.4

# Udp Library

Components used : 
- **lwIP** tcp/ip stack.
- **esp_timer** for timeouts

Create socket with udp : SOCK_DGRAM, auto ip protocol (we could chose proto ip udp?)

Set timeout of receiving with timeval type 

