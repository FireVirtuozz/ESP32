# Project for ESP32

This project is aiming to learn all capacities of ESP32 with ESP-IDF framework.
It is implemented:

- Wifi library : AP / STA / APSTA config & encrypted NVS credentials & scan & debug
- WS library : simple WS server to catch WS commands
- UDP library : UDP IPv4 server to catch UDP commands & debug
- OTA library : flash through wifi, using github .bin version check
- Screen library : minimalist screen controller for SSD1306
- LCD library : screen controller of SSD1306 for complex animations
- NVS library :  Load & Save String, int (int32), blob (uint8) to NVS encrypted
- LED library : GPIO debug & control, generate PWM with LEDC, fade PWMs
- MQTT library : Handle MQTT publish logs & subscribe topics, using HiveMQ; Queue to send logs when Wifi is up.

# Upgrades

- Logs through selected network (udp, ws, mqtt..)
- Ping & watchdog to reconnect
- Heap & stack & core monitoring
- Add new sensors (MPU, Hall, HC‑SR04)
- Voltage, current & temperature monitoring
- Network benchmark : ESP-NOW, Wifi-Mesh, LoRa
- Ifndef for each non-used librairies, edit with KConfig
- Sleep modes
- Add LiDAR sensor & AI calculus through server for autonomous drive
- SecureBoot : protect from unknown firmware flashes (todo at the end, irreversible & hard to implement, especially for OTA flash)
- Details & debug about : OTA, MQTT, WS, Screen & LCD, Wifi low-level

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

# NVS Library

# Wifi Library

**NVS has to be enabled before**

For wifi operations.

You can enable the ESP32 in AP, STA, or APSTA mode with:

```bash
//Initialize wifi station, waiting to get IP from router
void wifi_init_sta(void);

//Initialize ESP wifi on AP
void wifi_init_ap(void);

//Initialize ESP wifi on APSTA
void wifi_init_apsta(void);
```

Add your *known networks* to the list in wifiLib in order to connect as STA to your network. You can also edit the priority of connection.


# OTA Library

For Over The Air Updates. Using Github & version check.

Modify the version in CMakeLists.txt : `set(PROJECT_VER "1.7.0")`

To use, send the command 'OTA_UPDATE' via MQTT.

# UDP Library

**Wifi has to be enabled before**

Supports UDP server communication for IPv4.

# WS Library

**Wifi has to be enabled before**

# Other components to explore

## mDNS

Could be useful for finding devices on local network with dynamic IP, instead of setup a static IP DHCP. mDNS allows to communicate via esp-32.local instead of 192.162.1.4

## ESP-NOW

Useful for ESP32 inter-comms. Very fast (1ms) & long range. It is like radio comms between ESPs. Powerful for long-range fast customized comms, such as sending commands.

Max theorical range : 1km?
Indoor : 20m?
Outdoor : 100m?
Max size : 250 bytes?

## SPIFFS / LittleFS

Filesystem on flash. Store logs, configuration files, assets for web server. Could use Virtual Filesystem Component (VFS).

## MCPWM

For motor H-Bridge control.

## UART

For uart comms.

## Power management

Deep sleep, light sleep. Can wake on timer or GPIO event. Very useful for battery-powered setups

## FTM – Wi‑Fi

Wi‑Fi Fine Timing Measurement (802.11mc). Can measure distance between two Wi‑Fi devices supporting FTM.

## ESP S3

Config: 

Flash size 16Kb

# Frames

This presents frames that are sent through network.
Some are fragmented with:

[id u32][frag_total u8][frag_idx u8][esp_id u8]

Big-endian.

## Sensors
[type u8][esp_id u8][timestamp u32] [packet_payload]

Little-endian.

Types of sensors
0) KY-003 (analog)
1) HC-SR04
2) MPU9250
3) INA226
4) RFID-RC522

## Logs
[esp_id u8][timestamp u32][level LogLevel=u8][tag_length u8][tag_str][msg_str]

Little-endian.

## Dumps
[esp_id u8][timestamp u32][lib_length u8][lib_str][name_length u8][name_str][msg_str]

Little-endian.

## Video
[payload]

## Commands
[command_type u8][payload_length u8][payload]

# ESPs network

I implemented a network of EPSs communicating through UDP to PC and ESP-NOW between each other.

Do not forget that ESP-NOW works with wifi interface, becareful if you use an AP config, the address is MAC+1.

## ESP 0

ESP32-S3 N16R8 (16mb flash, 8mb psram)

No encryption (to free a bit of space in DMA for Camera)

Mac : 90:70:69:07:93:60

ESP handling a 120° camera OV2670. It is connected to the **ESP 3** AP gateway (STA config). Sends its frames to PC through UDP.

## ESP 1

ESP32 N4 (4mb flash)

Encryption enabled

Mac : B0:CB:D8:E8:68:38

Handling sensors. HC-SR04, MPU. Sends its frames to **ESP 3** through ESP-NOW.

## ESP 2 

ESP32 N4

Encryption enabled

Mac : A4:F0:0F:66:B8:50

## ESP 3

ESP32-S3 N16R8

Encryption enabled

Mac : 

ESP controlling the car. Receiving ESP-NOW frames from others ESPs and dispatch UDP to PC, commands UDP from PC, sending UDP things to PC.
