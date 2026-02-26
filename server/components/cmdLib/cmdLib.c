#include "cmdLib.h"
#include "ledLib.h"
#include "logLib.h"

#define BUFFER_SIZE_PAYLOAD_GAMEPAD 7 //6 axes, 8 buttons in 1 byte
#define BUFFER_SIZE_PAYLOAD_ANDROID 2 //2 axes

//frame : [cmd_type][cmd_size][payload]
#define BUFFER_IDX_TYPE 0
#define BUFFER_IDX_PAYLOAD_SIZE 1
#define BUFFER_IDX_PAYLOAD 2

const char* TAG = "cmd_library";

esp_err_t get_cmd_type(const int8_t *buf, command_type_t *cmd_type) {

    if (buf == NULL || cmd_type == NULL) return ESP_ERR_INVALID_ARG;

    if (buf[BUFFER_IDX_TYPE] >= CMD_TYPE_MAX) return ESP_ERR_INVALID_ARG;

    *cmd_type = (command_type_t)buf[BUFFER_IDX_TYPE];
    return ESP_OK;
}

esp_err_t gamepad_from_buffer(const int8_t *buf, gamepad_t *gamepad) {

    if (buf == NULL || gamepad == NULL) return ESP_ERR_INVALID_ARG;

    if (buf[BUFFER_IDX_PAYLOAD_SIZE] != BUFFER_SIZE_PAYLOAD_GAMEPAD) return ESP_ERR_INVALID_SIZE;

    gamepad->leftX        = buf[BUFFER_IDX_PAYLOAD + 0];
    gamepad->leftY        = buf[BUFFER_IDX_PAYLOAD + 1];
    gamepad->rightX       = buf[BUFFER_IDX_PAYLOAD + 2];
    gamepad->rightY       = buf[BUFFER_IDX_PAYLOAD + 3];
    gamepad->rightTrigger  = buf[BUFFER_IDX_PAYLOAD + 4];
    gamepad->leftTrigger = buf[BUFFER_IDX_PAYLOAD + 5];
    gamepad->buttons      = (uint8_t)buf[BUFFER_IDX_PAYLOAD + 6];
    
    return ESP_OK;
}

esp_err_t get_gamepad_value(const gamepad_t *gamepad, const gamepad_field_t field, int8_t *val) {

    if (gamepad == NULL || val == NULL) return ESP_ERR_INVALID_ARG;

    if (field >= GAMEPAD_FIELD_MAX) return ESP_ERR_INVALID_ARG;

    switch (field)
    {
    case GAMEPAD_AXIS_LEFT_X:
        *val = gamepad->leftX;
        break;
    case GAMEPAD_AXIS_LEFT_Y:
        *val = gamepad->leftY;
        break;
    case GAMEPAD_AXIS_RIGHT_X:
        *val = gamepad->rightX;
        break;
    case GAMEPAD_AXIS_RIGHT_Y:
        *val = gamepad->rightY;
        break;
    case GAMEPAD_TRIGGER_LEFT:
        *val = gamepad->leftTrigger;
        break;
    case GAMEPAD_TRIGGER_RIGHT:
        *val = gamepad->rightTrigger;
        break;
    case GAMEPAD_BUTTON_0:
        *val = (gamepad->buttons & 0x01) ? 1 : 0;
        break;
    case GAMEPAD_BUTTON_1:
        *val = (gamepad->buttons & 0x02) ? 1 : 0;
        break;
    case GAMEPAD_BUTTON_2:
        *val = (gamepad->buttons & 0x04) ? 1 : 0;
        break;
    case GAMEPAD_BUTTON_3:
        *val = (gamepad->buttons & 0x08) ? 1 : 0;
        break;
    case GAMEPAD_BUTTON_4:
        *val = (gamepad->buttons & 0x10) ? 1 : 0;
        break;
    case GAMEPAD_BUTTON_5:
        *val = (gamepad->buttons & 0x20) ? 1 : 0;
        break;
    case GAMEPAD_BUTTON_6:
        *val = (gamepad->buttons & 0x40) ? 1 : 0;
        break;
    case GAMEPAD_BUTTON_7:
        *val = (gamepad->buttons & 0x80) ? 1 : 0;
        break;
    
    default:
        break;
    }

    return ESP_OK;
}

esp_err_t android_from_buffer(const int8_t *buf, android_t *android) {

    if (buf == NULL || android == NULL) return ESP_ERR_INVALID_ARG;

    if (buf[BUFFER_IDX_PAYLOAD_SIZE] != BUFFER_SIZE_PAYLOAD_ANDROID) return ESP_ERR_INVALID_SIZE;

    android->sliderX        = buf[BUFFER_IDX_PAYLOAD + 0];
    android->sliderY        = buf[BUFFER_IDX_PAYLOAD + 1];

    return ESP_OK;
}

esp_err_t get_android_value(const android_t *android, const android_field_t field, int8_t *val) {
    if (android == NULL || val == NULL) return ESP_ERR_INVALID_ARG;

    if (field >= ANDROID_MAX_FIELD) return ESP_ERR_INVALID_ARG;

    switch (field)
    {
    case ANDROID_SLIDER_X:
        *val = android->sliderX;
        break;
    case ANDROID_SLIDER_Y:
        *val = android->sliderY;
        break;
    
    default:
        break;
    }

    return ESP_OK;
}

esp_err_t apply_gamepad_commands(const gamepad_t *gamepad) {
    if (gamepad == NULL) return ESP_ERR_INVALID_ARG;

    //leftX --> direction
    ledc_angle((int16_t)(((int16_t)gamepad->leftX + 100) * 9 / 10));

    if (gamepad->rightTrigger > -95 && gamepad->leftTrigger > -95) { //2 triggers activated --> brake
        ledc_motor(0);
    } else if (gamepad->rightTrigger > -95) { //right --> accelerate
        ledc_motor((int16_t)(((int16_t)gamepad->rightTrigger + 100) / 2));
    } else if (gamepad->leftTrigger > -95) { //left --> reverse
        ledc_motor((int16_t)(((int16_t)gamepad->leftTrigger + 100) / (-2)));
    } else { //nothing --> brake
        ledc_motor(0);
    }

    return ESP_OK;
}

esp_err_t apply_android_commands(const android_t *android) {
    if (android == NULL) return ESP_ERR_INVALID_ARG;

    ledc_angle((int16_t)(((int16_t)android->sliderX + 100) * 9 / 10)); //direction
    ledc_motor((int16_t)((int16_t)android->sliderY)); //accel

    return ESP_OK;
}

void dump_gamepad(const gamepad_t *gamepad) {
    if (gamepad == NULL) {
        log_msg(TAG, "Error invalid argument"); 
        return;
    }

    log_msg(TAG, "Gamepad raw: [%d,%d,%d,%d,%d,%d][%d,%d,%d,%d,%d,%d,%d,%d]", 
        gamepad->leftX, gamepad->leftY, gamepad->rightX,
        gamepad->rightY, gamepad->leftTrigger, gamepad->rightTrigger,
        (gamepad->buttons & 0x01) ? 1 : 0, (gamepad->buttons & 0x02) ? 1 : 0, 
        (gamepad->buttons & 0x04) ? 1 : 0, (gamepad->buttons & 0x08) ? 1 : 0,
        (gamepad->buttons & 0x10) ? 1 : 0, (gamepad->buttons & 0x20) ? 1 : 0, 
        (gamepad->buttons & 0x40) ? 1 : 0, (gamepad->buttons & 0x80) ? 1 : 0);    

}

void dump_android(const android_t *android) {
    if (android == NULL) {
        log_msg(TAG, "Error invalid argument"); 
        return;
    }

    log_msg(TAG, "Android raw: [%d,%d]", 
        android->sliderX, android->sliderY); 
}