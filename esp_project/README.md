# esp_project

ESP-IDF (C) firmware for my ESP32 experiments. I started it to learn what the ESP32 can do, and it is now the firmware of the RC car and of the Zigbee hub.

<!-- TODO: add a photo of the boards / the car here -->

See the [main README](../README.md) for the project overview.

## Libraries

- **camera lib**
  - JPG stream over UDP (tested with OV5640 and OV2640), using the [esp32-camera](https://components.espressif.com/components/espressif/esp32-camera/versions/2.1.6/readme?language=en) component
- **zigbee lib**, using [esp-zigbee-sdk](https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32/introduction.html) and [esp-zigbee-lib](https://components.espressif.com/components/espressif/esp-zigbee-lib/versions/2.0.3/readme)
  - simple on/off LED
- **nvs lib**
  - easily edit NVS data
- **sensors lib**: read sensors through the right peripheral
  - I2C: complex sensors like IMU (MPU6050, BNO085), ToF (VL53L1X), voltage monitoring (INA226)
  - GPIO: digital signals, ISR based (ultrasonic HC-SR04, "slow" rotary encoder, reed switch, buttons...)
  - PCNT: counting pulses of "fast" encoders, like on the power axis of a car
    - Use it instead of ISRs when events are very frequent, to avoid starving the CPU: the hardware module does the counting on its own, we just read it at some period.
    - It can also filter events (ex: remove bounces).
  - RMT: sensors with specific timings, like one-wire sensors (DHT11) or an infrared receiver (NEC protocol)
  - ADC: analog sensors (potentiometers / joystick, photosensors, linear hall, vibration...)
  - SPI: fast sensors (compared to I2C), like an RFID card reader
  - ESP: info about the chip itself (SoC temperature, CPU %, memory)
- **wifi lib** *(TODO: separate AP / STA, debug helper)*
  - AP / STA / APSTA configs
  - auto connect to known networks stored in encrypted NVS
- **actuators lib**
  - addressable RGB LED (RMT)
  - passive buzzer (LEDC / PWM)
  - H-bridge control (LEDC, MCPWM is a TODO) with custom motor curves
  - servo (LEDC)
  - simple LED (GPIO), two-color LED and RGB LED (LEDC)
- **ws lib**
  - simple websocket server
- **udp lib**
  - sends messages safely through a queue (fragmentation available)
  - receives messages
- **espnow lib**
  - sends messages to other ESPs safely through a queue, using [ESP-NOW](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html). Fragmentation is useful here because the max size is 250 bytes.
  - receives messages from other ESPs
- **mqtt lib**
  - receives MQTT commands over TLS, using the [mqtt](https://components.espressif.com/components/espressif/mqtt/versions/1.1.0/readme) component and the [Espressif certificate bundle](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_crt_bundle.html)
- **ota lib**
  - flash the ESP over the air using an HTTP server
- **cmd lib**
  - parses the messages received from the controller and applies them to the motor through the H-bridge
- **log lib**
  - custom log system, logs can be redirected through UDP or ESP-NOW
- **system lib**
  - useful info about the chip (eFuse blocks, CPU, DRAM, PSRAM, APP, bootloader)
- **screen lib**
  - minimalist library for tiny screens like SSD1306
- **lcd lvgl lib**
  - library for bigger screens (with animations), using [LCD](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/lcd/index.html) and [lvgl](https://components.espressif.com/components/lvgl/lvgl/versions/9.5.0/readme)

## RC car firmware

- **UDP**: one port receives commands, another receives configs. Logs, sensor data, dumps and video are sent to the station (queues, with fragmentation for dumps and video).
- **OTA** over HTTP, Wi-Fi in STA or AP mode.
- **Motor control**: a timer task (20 ms) applies the PWM. Acceleration profile is configurable (cosine, linear, exponential) from the error between current and target motor value.
- **Safety**: emergency brake (reverse PWM to cancel the inertia), and front / rear blocking using the HC-SR04.
- **Buzzer**: melodies played from a queue.

### Sensor tasks (priority 5)

| Sensor | Period / mode |
|---|---|
| BNO085 | 10 ms |
| TOF400C | 10 ms |
| HC-SR04 front / rear | 50 ms |
| Battery ADC, ACS758 | 50 ms |
| FC-33 (PCNT) | read every 20 ms |
| D500 lidar | UART 230400 baud, 200 ms timeout, sent over UDP in batches of 20 packets |
| BZ181 GNSS | UART 115200 baud, 200 ms timeout, sent over UDP every 1 s |
| ESP32 stats | 500 ms, priority 3 |

### Other tasks

| Task | ESP | Priority |
|---|---|---|
| CTRL_TIMER (motor control, 20 ms) | control | 22 |
| UDP_SERV_CMD (commands, pinned to core 0) | control | 15 |
| ADC_MANAGER (continuous read, averages, 10 ms) | control | 5 |
| BUZZER | control | 4 |
| UDP_SERV_CFG (camera, acceleration, OTA configs) | both | 4 |
| UDP clients: logs, sensors, dumps, configs | both | 4 |
| UDP_CLI_VIDEO, JPG_STREAM_UDP | camera | 4 |
| OTA, WIFI_SCAN | both | 5 |

For reference, the ESP-IDF system tasks run at higher priorities (Wi-Fi 23, IPC 24, timer 22, system events 20, lwIP 18), so the control timer stays below the network stack.

## Build and flash

```bash
. ./set_env.sh          # load the ESP-IDF environment
idf.py set-target esp32s3      # or esp32 / esp32c6
idf.py menuconfig
idf.py build
idf.py flash monitor
```

Docker setup (with USB passthrough on Windows/WSL) is in the [main README](../README.md#docker-esp-idf).

Handy commands:

```bash
idf.py create-project my_project
esptool chip-id
esptool flash-id
esptool --chip esp32 image-info build/app.bin
esptool --chip esp32 image-info ./build/bootloader/bootloader.bin
idf.py size
idf.py size-components
```

## Menuconfig

```bash
idf.py menuconfig
```

- **Compiler options**: optimization level, "optimize for size"
- **Partition table**: custom CSV (needed for OTA and others)
- **Serial flasher config**: flash size depends on the chip (4 MB on ESP32 N4, 16 MB on ESP32-S3 N16R8)
- **OTA**: allow HTTP for OTA if the firmware is served over plain HTTP
- **Wi-Fi** (when an ESP is an AP gateway):
  - DNS: enable DNS server settings with netif, enable DNS fallback (8.8.8.8)
  - LWIP: enable IP forwarding, enable NAT and NAPT
- **WebSocket**: HTTP Server, enable websocket support
- **UDP**: default UDP receive mailbox size = 32 (a bigger value reduces packet loss)

## Using the libraries

Some libraries depend on others, so the init order matters: **NVS first**, then **Wi-Fi**, then UDP / WS / MQTT / OTA.

Wi-Fi modes:

```c
void wifi_init_sta(void);    // station: waits for an IP from the router
void wifi_init_ap(void);     // access point
void wifi_init_apsta(void);  // both
```

Add your *known networks* to the list in `wifiLib` to connect as STA (you can also set their priority). Credentials are stored in encrypted NVS.

**OTA**: the station starts a small HTTP server on request, and the ESP is told to update through the config UDP port.

## ESPs network

The ESPs talk to the PC through UDP over Wi-Fi. ESP-NOW is implemented as a library, and planned for the custom controller of the car.

Careful: ESP-NOW uses the Wi-Fi interface. If an ESP is in AP config, its address is the MAC + 1.

On the [RC car](../README.md):

| ESP | Chip | Role |
|---|---|---|
| ESP 0 | ESP32-S3 N16R8 | Control board: motor, servo, sensors, buzzer. Receives the commands and configs over UDP. |
| ESP 1 | ESP32-S3 (camera board) | JPG stream over UDP, camera config. <!-- TODO: check, I think encryption is disabled on this one to free DMA memory for the camera --> |

## Frames

Frames sent through the network. Some are fragmented, with this header (**big-endian**):

```
[id u32][frag_total u8][frag_idx u8][esp_id u8]
```

All the others are **little-endian**.

**Sensors**
```
[type u8][esp_id u8][timestamp u32][packet_payload]
```

**Logs**
```
[esp_id u8][timestamp u32][level u8][tag_length u8][tag_str][msg_str]
```

**Dumps**
```
[esp_id u8][timestamp u32][lib_length u8][lib_str][name_length u8][name_str][msg_str]
```

**Video**
```
[fragment_header][payload]
```

**Commands**
```
[command_type u8][payload_length u8][payload]
```

## ESP-IDF notes

**Bootloader**: initializes clocks, flash and memory, then loads the application from flash.

**Memory**
- IRAM: fast code (interrupts, critical paths)
- DRAM: data and task stacks
- PSRAM: big buffers and temporary stacks
- Use `idf.py size` and `idf.py size-components` to check usage

**External stack** (PSRAM instead of internal DRAM for the function stack)
- Useful when the DRAM is almost full, and for heavy functions (JSON, TLS, logs)
- Slower, and not suitable for ISRs or timing-critical code

**App level tracing**: lightweight tracing for fine debugging, cheaper than full logging. Can be enabled in menuconfig.

## Ideas / TODO

- Refactoring
- Sleep modes (deep sleep / light sleep, wake on timer or GPIO), useful on battery
- Secure Boot (irreversible, and hard to combine with OTA, so at the very end)

## Reference

[ESP32 technical reference manual](https://documentation.espressif.com/esp32_technical_reference_manual_en.pdf)