#ifndef OTA_LIB_H_
#define OTA_LIB_H_

#include <stdatomic.h>
static atomic_bool ota_lock = false;

void ota_init();

#endif