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

#using /mnt/c from wsl (unconvenient for performance)
cd D:\ESP32
wsl
docker run -it --rm --network host --device=/dev/ttyUSB0 -v $(pwd):/workspace esp32-idf

#better for performance using wsl only and update files with github
wsl
cd ~/projects/ESP32
docker run -it --rm --network host -v $(pwd):/workspace esp32-idf:latest
```

Launch esp-idf environment
```bash
. $IDF_PATH/export.sh
```

# esp32

https://documentation.espressif.com/esp32_technical_reference_manual_en.pdf

## Commands

```bash
idf.py create-project my_project
idf.py set-target esp32
idf.py menuconfig
idf.py build
idf.py flash
idf.py monitor
esptool chip-id
esptool flash-id
```

## oled ssd1306 screen display

https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf

## H-Bridge BTS7960

https://www.handsontec.com/dataspecs/module/BTS7960%20Motor%20Driver.pdf

## Servo MG996R

https://www.handsontec.com/dataspecs/motor_fan/MG996R.pdf

## ESP-IDF Components

### System

Bootloader
- BIOS-like, initializes clocks, flash, and memory  
- Loads the application from flash 

Useful commands
```bash
esptool --chip esp32 image-info build/app.bin
esptool --chip esp32 image-info ./build/bootloader/bootloader.bin
idf.py size
idf.py size-components
```

App Level Tracing
- Lightweight tracing for fine-grained debugging
- Less costly than full logging
- Can be enabled via menuconfig

External stack:
- Uses PSRAM instead of internal DRAM for function stack (but slower)
- Useful when internal DRAM is close to full
- Helps avoid stack overflows for heavy functions (JSON, TLS, logs)
- Not suitable for ISR or timing-critical code

Memory overview
- IRAM: fast code (interrupts, critical paths)
- DRAM: data and task stacks
- PSRAM: large buffers and temporary stacks
- Use idf.py size and idf.py size-components to check usage

# Java windows controller & wss mqtt

**Workflow** : Get inputs with LWJGL, stack into bytes and publish them to HiveMQ WSS MQTT brocker. Using bytes for better performance than JSON with **jackson** (rate reduced by ~10).

## Controller input : LWJGL

## MQTT client : HiveMQ

## Websocket : Netty

# Backend to hid credential

.properties maybe

## Node.js 

Maybe useful to hid credentials.

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