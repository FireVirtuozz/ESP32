# Project for ESP32

# Upgrades

- Debug functions for everything
- ifndef for each non-used librairies, edit with KConfig

# WS Library


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
