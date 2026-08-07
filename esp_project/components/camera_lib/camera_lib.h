#ifndef CAMERA_LIB_H_
#define CAMERA_LIB_H_

#include <esp_err.h>

#define CAMCFG_FRAME_SIZE          31

esp_err_t camera_init();

void apply_camera_config(const uint8_t *buffer, size_t len);

#endif