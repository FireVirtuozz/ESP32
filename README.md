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

In APSTA mode : 

menuconfig -> component config 

DNS -> enable dns server setting with netif
DNS -> Enable DNS fallback support 8.8.8.8

Enable IP forwarding
Enable NAT & NAT port mapping

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