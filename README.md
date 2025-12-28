# Useful

Docker
```bash
docker build -t esp32-idf .
docker run -it --rm --network host --device=/dev/ttyUSB0 -v $(pwd):/workspace esp32-idf
```

Setup usb windows -> linux docker
```bash
ls /dev/ttyUSB*
usbipd list
usbipd bind --busid 2-3
usbipd attach --wsl --busid 2-3
```

Launch container (windows powershell)
```bash
usbipd attach --wsl --busid 2-3
cd D:\ESP32
wsl
docker run -it --rm --network host --device=/dev/ttyUSB0 -v $(pwd):/workspace esp32-idf
```

Launch esp-idf environment
```bash
. $IDF_PATH/export.sh
```

# esp32 

## Commands

```bash
idf.py create-project my_project
idf.py set-target esp32
idf.py menuconfig
idf.py build
idf.py flash
idf.py monitor
```

## oled ssd1306 screen display

https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf

Works with I²C; See for server/components/screenLib for use.

## ESP-IDF Components

### System

Bootloader : like the BIOS

```bash
esptool --chip esp32 image-info build/app.bin
esptool --chip esp32 image-info ./build/bootloader/bootloader.bin
idf.py size
idf.py size-components
```

### GPIO

For gpio control; used in led

https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/gpio.html

### NVS

For storage of data within ESP; Used for wifi, led, getting previous data...

https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html

Do not forget to erase flash if data not used anymore or issues.

```bash
idf.py erase-flash
```
### I²C

For I²C bus communication; Used for screen display ssd1306.

### Semaphore

Mutex used in NVS, Led, to avoid having R/W issues  

https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos_idf.html#semaphore-api

### Wifi

Wifi component;

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


### WS

esp http server; using uri ws handlers;

https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_server.html

menuconfig -> component config

activate websocket support

### ESP log & err

Useful logging;

# Git

```bash
git init
git remote add origin https://github.com/FireVirtuozz/ESP32.git
git branch -M main
git add .
git status
git commit -m "new"
git push -u origin main
git add -A
```