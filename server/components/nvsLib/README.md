# NVS Library for ESP32

Non-volatile storage (NVS) library is designed to store key-value pairs in flash. It is better for many small values, rather than large values.

By default, the NVS is allocating in RAM. If you want it to be on the SPIRAM, if the chip has PSRAM, edit the NVS SPIRAM use in `menuconfig`

Add a line *nvs* in partition.

## TODO : 
- NVS for each int types, not only i32
- Special default value
- For performance, save function for a value that could be commit 1/100 changes for instance
 (on ledc better)
- NVS Count for known networks 

## Components used

- **nvs / nvs_flash** to perform NVS operations
- **FreeRTOS** for thread-safe operations with mutex

## Key concepts

Partition : using default one, `nvs`

Namespace : Using default one, `storage`

Partition (custom one could be used) -> Namespace -> Key : Val

**Encryption** 

menuconfig :
- Security Features -> Enable Flash Encryption
- Partition table -> offset 0xD000 (bootloader's size increase with flash encryption)
- NVS -> Enable NVS encryption

Add partition `nvs_keys` in custom partition, erase it if already used. Make sur the encryption is in Development mode.

```bash
# nvs_keys, data, nvs_keys,  , 0x1000, encrypted
parttool.py erase_partition --partition-subtype nvs_keys
```

To flash, use

```bash
idf.py encrypted-flash
```

How it works?

`nvs_flash_init` automatically generates the AES-256 XTS encryption keys.

It is possible to do it manually. See in 
https://github.com/espressif/esp-idf/tree/97d95853572ab74f4769597496af9d5fe8b6bdea/examples/security/flash_encryption

**AES-256** : It is an algorithm (*cipher*) to encrypt the data & makes it unreadable using the key. To get the data, it has to do the invert algorithm, but in order to work the key is needed. These keys are stored in the partition `nvs_keys`. When a `nvs_get / nvs_set` is done, it automatically do these steps.

**XTS** : Encryption mode for drives & flash. AES encrypts data in blocks, and XTS incorporates the flash block address as a *tweak* so that identical blocks stored at different locations are encrypted differently.

If someone wants to get those keys, he has to look into `nvs_keys`. However, with flash encryption, partition are also encrypted so it is very hard to get the data. The other way is to flash another firmware and do an `nvs_get` operation. To prevent this, *secure boot* has to be enabled.

