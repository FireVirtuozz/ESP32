#ifndef EFUSELIB_H_
#define EFUSELIB_H_

#include <hal/efuse_hal.h>
#include <esp_log.h>
#include "mqttLib.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_idf_version.h"
#include "esp_mac.h"
#include "esp_app_desc.h"
#include "esp_cpu.h"

//print chip info
void print_chip_info();

//restart the chip
void restart_chip();

#endif