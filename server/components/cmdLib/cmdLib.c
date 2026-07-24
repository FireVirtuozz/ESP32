#include "cmdLib.h"
#include "ledLib.h"
#include "logLib.h"

#define BUFFER_SIZE_PAYLOAD_GAMEPAD 7 //6 axes, 8 buttons in 1 byte
#define BUFFER_SIZE_PAYLOAD_ANDROID 2 //2 axes

//frame : [cmd_type][cmd_size][payload]
#define BUFFER_IDX_TYPE 0
#define BUFFER_IDX_PAYLOAD_SIZE 1
#define BUFFER_IDX_PAYLOAD 2

static const char* TAG = "cmd_library";

static volatile drive_mode_e drive_mode = DEFAULT;

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

drive_mode_e get_drive_mode() {
    return drive_mode;
}

esp_err_t apply_gamepad_commands(const gamepad_t *gamepad) {
    if (gamepad == NULL) return ESP_ERR_INVALID_ARG;

    static bool last_dpadright = false;
    static bool last_dpadleft = false;

    static bool last_dpadup = false;
    static bool last_dpaddown = false;

    bool current_dpadleft = (gamepad->buttons & 0b00100000);
    bool current_dpadright = (gamepad->buttons & 0b01000000);

    bool current_dpadup = (gamepad->buttons & 0b10000000);
    bool current_dpaddown = (gamepad->buttons & 0b00010000);

    log_msg(TAG, "buttons: 0b%c%c%c%c%c%c%c%c | L:%d R:%d U:%d D:%d",
        (gamepad->buttons & 0x80) ? '1' : '0',
        (gamepad->buttons & 0x40) ? '1' : '0',
        (gamepad->buttons & 0x20) ? '1' : '0',
        (gamepad->buttons & 0x10) ? '1' : '0',
        (gamepad->buttons & 0x08) ? '1' : '0',
        (gamepad->buttons & 0x04) ? '1' : '0',
        (gamepad->buttons & 0x02) ? '1' : '0',
        (gamepad->buttons & 0x01) ? '1' : '0',
        current_dpadleft, 
        current_dpadright,
        current_dpadup,
        current_dpaddown
    );

    if (!last_dpadleft && current_dpadleft) {
        if (drive_mode > DEFAULT) {
            drive_mode--;
        }
    }

    if (!last_dpadright && current_dpadright) {
        if (drive_mode < EXPERT) {
            drive_mode++;
        }
    }

    last_dpadleft = current_dpadleft;
    last_dpadright = current_dpadright;

    last_dpadup = current_dpadup;
    last_dpaddown = current_dpaddown;

    //leftX --> direction
    ledc_angle((int16_t)(((int16_t)gamepad->leftX + 100) * 9 / 10));

    // 2. Calcul de la vitesse de base (Brute, de -100 à +100)
    int16_t target_speed = 0;

    if (gamepad->rightTrigger > -95 && gamepad->leftTrigger > -95) { 
        // 2 gâchettes activées --> frein
        target_speed = 0;
    } else if (gamepad->rightTrigger > -95) { 
        // Gâchette droite --> marche avant (0 à 100)
        target_speed = (int16_t)(((int16_t)gamepad->rightTrigger + 100) / 2);
    } else if (gamepad->leftTrigger > -95) { 
        // Gâchette gauche --> marche arrière (-100 à 0)
        target_speed = (int16_t)(((int16_t)gamepad->leftTrigger + 100) / (-2));
    } else { 
        // Rien --> frein
        target_speed = 0;
    }

    float speed_factor = 1.0f;
    
    // On imagine que tu as ajouté 'drive_mode' dans ta structure gamepad_t
    switch (drive_mode) {
        case DEFAULT:  // Mode ECO (Bridé à 40% de la puissance)
            speed_factor = 0.25f; 
            break;
        case MIDDLE:  // Mode NORMAL (Bridé à 70% de la puissance)
            speed_factor = 0.40f; 
            break;
        case ADVANCED:  // Mode SPORT (Pleine puissance)
            speed_factor = 0.70f; 
            break;
        case EXPERT:  // Mode SPORT (Pleine puissance)
            speed_factor = 1.00f; 
            break;
        default: // Sécurité si le mode reçu est corrompu/inconnu
            speed_factor = 0.40f; 
            break;
    }

    // Applique le coefficient multiplicateur
    int16_t final_speed = (int16_t)((float)target_speed * speed_factor);

    // 4. Envoi de la commande finale bridée au moteur
    ledc_motor(final_speed * 10);

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

esp_err_t cmd_dispatch(const int8_t *data) {
    esp_err_t err;
    command_type_t type = CMD_TYPE_MAX;
    err = get_cmd_type(data, &type);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) getting command type", 
                esp_err_to_name(err));
    }
    switch (type) {
        case CMD_GAMEPAD: //gamepad type control

            gamepad_t gamepad;
            err = gamepad_from_buffer(data, &gamepad);
            if (err != ESP_OK) {
                log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) getting gamepad from buffer", 
                        esp_err_to_name(err));
                break;
            }

            err = apply_gamepad_commands(&gamepad);
            if (err != ESP_OK) {
                log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) applying gamepad commands", 
                        esp_err_to_name(err));
                break;
            }
            break;
        
        case CMD_ANDROID: //android type control

            android_t android;
            err = android_from_buffer(data, &android);
            if (err != ESP_OK) {
                log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) getting android from buffer", 
                        esp_err_to_name(err));
                break;
            }
            
            err = apply_android_commands(&android);
            if (err != ESP_OK) {
                log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) applying android commands", 
                        esp_err_to_name(err));
                break;
            }
            break;
        
        default:
            log_msg_lvl(ESP_LOG_WARN, TAG, "Command type not valid");
            break;
    }
    return err;
}

void reset_command() {
    ledc_motor(0);
    ledc_angle(90);
}