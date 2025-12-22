# intro

commande docker
docker build -t esp32-idf .
docker run -it --rm --network host --device=/dev/ttyUSB0 -v $(pwd):/workspace esp32-idf

commande setup usb
ls /dev/ttyUSB*
usbipd list
usbipd bind --busid 2-3
usbipd attach --wsl --busid 2-3

commande lancement wsl
usbipd attach --wsl --busid 2-3

lancer environnement esp-idf
. $IDF_PATH/export.sh

# esp32 

## Commandes

```bash
idf.py create-project mon_projet
idf.py set-target esp32
idf.py menuconfig
idf.py build
idf.py flash
idf.py monitor
```

## Serial flashing config

Disable download stub 
flasher utilise le petit programme temporaire qui rend le téléchargement du firmware plus fiable et rapide.

Flash SPI mode 
mode de communication SPI entre l’ESP32 et sa mémoire flash
DIO / DOUT / QIO / QOUT / OPI indiquent combien de lignes de données sont utilisées pour lire/écrire dans la flash

etc..

## oled ssd1306 screen display

https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf