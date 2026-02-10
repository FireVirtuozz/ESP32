# NVS Library for ESP32

Non-volatile storage (NVS) library is designed to store key-value pairs in flash. It is better for many small values, rather than large values.

By default, the NVS is allocating in RAM. If you want it to be on the SPIRAM, if the chip has PSRAM, edit the NVS SPIRAM use in `menuconfig`

Add a line *nvs* in partition.

TODO : 
- NVS Encryption
- NVS for each int types, not only i32
- Special default value
- For performance, save function for a value that could be commit 1/100 changes for instance
 (on ledc better)

## Components used

- **nvs / nvs_flash** to perform NVS operations
- **FreeRTOS** for thread-safe operations with mutex

## Key concepts

Partition : using default one, `nvs`

Namespace : Using default one, `storage`

Partition (custom one could be used) -> Namespace -> Key : Val

