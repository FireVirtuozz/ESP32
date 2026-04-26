# System library

This component is useful to print system info & monitor.

## Components used

- efuse
    * Obtain info on chip revision
    * Obtain info on efuse blocks
- mac
    * Obtain MAC address info
- chip_info
- mmu
    * Obtain info on MMU mapped blocks
- app_format
- bootloader_format
- partition
- cpu
- idf_version
    * Obtain ESP-IDF's version
- system
    * Obtain heap info

## Components to add

- Application Level Tracing
    * Transfer data to host using JTAG
- Console
    * Communicate with ESP through UART / USB console

## Useful EFuse commands

```bash
idf.py efuse-summary --port /dev/ttyUSB0
idf.py efuse-dump --port /dev/ttyUSB0
```