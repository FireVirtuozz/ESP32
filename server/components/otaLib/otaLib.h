#ifndef OTALIB_H_
#define OTALIB_H_

#include <stdatomic.h>
static atomic_bool ota_lock = false;

void ota_init();

#endif