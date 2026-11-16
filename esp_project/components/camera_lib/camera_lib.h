#ifndef CAMERA_LIB_H_
#define CAMERA_LIB_H_

#include <esp_err.h>

esp_err_t camera_init();

void apply_camera_config(const uint8_t *buffer, size_t len);

esp_err_t get_reg_cam(const uint8_t *buffer, size_t len);

esp_err_t set_reg_cam(const uint8_t *buffer, size_t len);

esp_err_t get_config_cam();

#endif