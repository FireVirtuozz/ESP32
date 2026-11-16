#ifndef OTA_LIB_H_
#define OTA_LIB_H_

#include <stdatomic.h>
extern atomic_bool ota_lock;

void ota_init();

#endif