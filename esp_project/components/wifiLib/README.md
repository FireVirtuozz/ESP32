# Wifi Library

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

# TODO

* Explain ESP-WIFI more
* Explain ESP-NETIF
* Explain ESP-EVENT
* Explain FreeRTOS groups
* Store "COUNT" in NVS for numbers of known networks