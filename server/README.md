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

- Function in cmdLib to convert gamepad -> pwm safely (not cast..) & avoid redundance
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

