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

### NVS

For storage of data within ESP; Used for wifi.

https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html

Do not forget to erase flash if data not used anymore.

```bash
idf.py erase-flash
```
### I²C

For I²C bus communication; Used for screen display ssd1306.

### Semaphore

https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos_idf.html#semaphore-api

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