#ifndef CMDLIB_H_
#define CMDLIB_H_

#include <inttypes.h>
#include <esp_err.h>

typedef enum command_type_et {
    CMD_GAMEPAD,
    CMD_ANDROID,
    CMD_TYPE_MAX,
} command_type_t;

typedef enum android_field_et {
    ANDROID_SLIDER_X,
    ANDROID_SLIDER_Y,
    ANDROID_MAX_FIELD,
} android_field_t;

typedef enum gamepad_field_et {
    GAMEPAD_AXIS_LEFT_Y,
    GAMEPAD_AXIS_LEFT_X,
    GAMEPAD_AXIS_RIGHT_Y,
    GAMEPAD_AXIS_RIGHT_X,
    GAMEPAD_TRIGGER_LEFT,
    GAMEPAD_TRIGGER_RIGHT,
    GAMEPAD_BUTTON_0,
    GAMEPAD_BUTTON_1,
    GAMEPAD_BUTTON_2,
    GAMEPAD_BUTTON_3,
    GAMEPAD_BUTTON_4,
    GAMEPAD_BUTTON_5,
    GAMEPAD_BUTTON_6,
    GAMEPAD_BUTTON_7,
    GAMEPAD_FIELD_MAX,
} gamepad_field_t;

typedef struct gamepad_st {
    int8_t leftY;
    int8_t leftX;
    int8_t rightY;
    int8_t rightX;
    int8_t rightTrigger;
    int8_t leftTrigger;
    uint8_t buttons;
} gamepad_t;

typedef struct android_st {
    int8_t sliderX;
    int8_t sliderY;
} android_t;

esp_err_t get_cmd_type(const int8_t *buf, command_type_t *cmd_type);

esp_err_t gamepad_from_buffer(const int8_t *buf, gamepad_t *gamepad);

esp_err_t get_gamepad_value(const gamepad_t *gamepad, const gamepad_field_t field, int8_t *val);

esp_err_t android_from_buffer(const int8_t *buf, android_t *android);

esp_err_t get_android_value(const android_t *android, const android_field_t field, int8_t *val);

esp_err_t apply_gamepad_commands(const gamepad_t *gamepad);

esp_err_t apply_android_commands(const android_t *android);

void dump_gamepad(const gamepad_t *gamepad);

void dump_android(const android_t *android);

#endif