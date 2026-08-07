# Projects around ESP32

## esp_project

This is the ESP-IDF repo. Features:
- zigbee library using [esp-zigbee-sdk](https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32/introduction.html) & [esp-zigbee-lib](https://components.espressif.com/components/espressif/esp-zigbee-lib/versions/2.0.3/readme) component
  * simple on/off led
- nvs library 
- - sensors lib
 * Get the data of sensors using peripherals such as:
   * I2C: complex sensors such as IMU (MPU6050, BNO085), TOF (VL53L1X), Voltage monitoring (INA226)
   * GPIO: digital signal - ISR related [Ultrasonic (HCSR04), "Slow" Rotation Encoder, Reed, Buttons..]
   * PCNT: couting GPIO ["fast" encoder such as on the power-axis of a car]
       * Use this instead of ISRs when there is a very frequent event to avoid starving CPU, as it is a module on is own that puts its counter to a shared memory. We just have to read it at some period.
       * Also it can be used to have a clear event and filter events (ex: remove bounces)
   * RMT: handles sensors that needs specific timings, such as one-wire sensors (DHT11), or infrared receiver using NEC protocol.
   * ADC: sensors delivering analogic signals [Potentiometers (Joystick), Photosensors, Linear Hall, Vibration..]
   * SPI: sensors delivering fast data (compared to I2C) [RFID car reader]
- wifi library [TODO: seperate AP / STA, debug helper]
    * AP/STA/APSTA configs
    * auto connect to known networks stored in encrypted-NVS
- actuators library
  * addressable rgb led using RMT
  * passive buzzer control using LEDC (PWM)
  * h bridge control using LEDC [MCPWM todo] & custom motor curves
  * servo control using LEDC
  * simple led using GPIO
  * two color led using LEDC
  * RGB led using LEDC
- camera lib [using [esp32-camera](https://components.espressif.com/components/espressif/esp32-camera/versions/2.1.6/readme?language=en) component]
- ws lib
  * simple websocket server
- udp lib
  * emits messages through a queue safely, fragmentation can be used
  * receives messages
- system lib
  * get all the useful info on the ESP chip (eFuse blocks, CPU, DRAM, PSRAM, APP, BOOTLOADER)
- screen lib
  * minimalist screen library for tiny screens like SSD1306
- lcd lvgl lib
    * screen libraries for large screens (animations..) using [LCD](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/lcd/index.html) & [lvgl](https://components.espressif.com/components/lvgl/lvgl/versions/9.5.0/readme) component
- cmd lib
  * Parse messages received by the controller and apply it to motor through h_bridge
- espnow lib
  * send messages to ESP safely using a queue & [ESP-NOW](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html). Fragmentation can be used and useful as max size is 250 bytes.
  * receives messages from other ESPs
- log lib
  * custom log system that allows to redirect logs through UDP or ESP-NOW 
- mqtt lib
  * receives MQTT commands from https using [mqtt](https://components.espressif.com/components/espressif/mqtt/versions/1.1.0/readme) component and [espressif certificate](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_crt_bundle.html)
- ota lib
  * flash the ESP "over the air" using HTTP server 

## rust_station

- udp
  * receives udp frames from ESPs
- controller
  * emits udp commands with a controller (ex: PS4/XBOX)
- egui: HMI
  * sensors: plots for some sensors (IMU, INA, BMP, ESP, Encoder, RFID car reader)
  * ota: choose and flash the ESP over the air
  * logs: print logs from ESPs
  * dump: print dumps from ESPs
  * tuning: tune and monitor the motor curve
  * commands: controller panel
  * camera: show camera image & edit its config
  * car: Car control panel with sensors info, estimated trajectory
- recorder
  *  record data from sensors to replay it later
- ai
  * inference: load weights from a pre-trained model and apply decisions like turn, forward
- train_ia
  * ia training with a simulator (random room with obstacles and estimated data from sensors) using burn crate
  * gui to monitor steps

## Docker

You can use this minimal Dockefile [windows-friendly] or the one from [espressif](https://github.com/espressif/esp-idf/blob/master/tools/docker/Dockerfile).

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