#include "camera_lib.h"

#if CONFIG_USE_UDPLIB
#include "udp_lib.h"
#endif

#include "ota_lib.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "log_lib.h"
#include "esp_camera.h"
#include "esp_timer.h"
#include "driver/ledc.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ESP32S3 (GOOUU TECH)
#define CAM_PIN_PWDN -1
#define CAM_PIN_RESET -1   //software reset will be performed
#define CAM_PIN_VSYNC 6
#define CAM_PIN_HREF 7
#define CAM_PIN_PCLK 13
#define CAM_PIN_XCLK 15
#define CAM_PIN_SIOD 4
#define CAM_PIN_SIOC 5
#define CAM_PIN_D0 11
#define CAM_PIN_D1 9
#define CAM_PIN_D2 8
#define CAM_PIN_D3 10
#define CAM_PIN_D4 12
#define CAM_PIN_D5 18
#define CAM_PIN_D6 17
#define CAM_PIN_D7 16

#define CAMCFG_OFF_MASK            0   // u32, 4 bytes
#define CAMCFG_OFF_FRAMESIZE       4   // u8
#define CAMCFG_OFF_BRIGHTNESS      5   // i8
#define CAMCFG_OFF_CONTRAST        6   // i8
#define CAMCFG_OFF_SATURATION      7   // i8
#define CAMCFG_OFF_SHARPNESS       8   // i8
#define CAMCFG_OFF_DENOISE         9   // u8
#define CAMCFG_OFF_QUALITY         10  // u8
#define CAMCFG_OFF_GAINCEILING     11  // u16
#define CAMCFG_OFF_COLORBAR        13  // u8
#define CAMCFG_OFF_WHITEBAL        14  // u8
#define CAMCFG_OFF_AWB_GAIN        15  // u8
#define CAMCFG_OFF_WB_MODE         16  // u8
#define CAMCFG_OFF_EXPOSURE_CTRL   17  // u8
#define CAMCFG_OFF_AEC2            18  // u8
#define CAMCFG_OFF_AE_LEVEL        19  // i8
#define CAMCFG_OFF_AEC_VALUE       20  // u16, 2 bytes
#define CAMCFG_OFF_GAIN_CTRL       22  // u8
#define CAMCFG_OFF_AGC_GAIN        23  // u8
#define CAMCFG_OFF_HMIRROR         24  // u8
#define CAMCFG_OFF_VFLIP           25  // u8
#define CAMCFG_OFF_DCW             26  // u8
#define CAMCFG_OFF_BPC             27  // u8
#define CAMCFG_OFF_WPC             28  // u8
#define CAMCFG_OFF_RAW_GMA         29  // u8
#define CAMCFG_OFF_LENC            30  // u8
#define CAMCFG_OFF_SPECIAL_EFFECT  31  // u8
#define CAMCFG_OFF_XCLK            32  // u16, en kHz
#define CAMCFG_OFF_PIXFORMAT       34  // u8

#define CAMCFG_MIN_SIZE      34                       // without pixformat (size received by STA)
#define CAMCFG_PAYLOAD_SIZE  35                     // size send by ESP
#define CAMCFG_FRAME_SIZE    (1 + CAMCFG_PAYLOAD_SIZE) // + type of cfg

// Safety clamp XCLK
#define CAM_XCLK_MIN_KHZ   6000
#define CAM_XCLK_MAX_KHZ   27000
#define CAM_MAX_FRAMESIZE  FRAMESIZE_VGA   // Buffer size in menuconfig at init

// Register SYSTEM_CTROL0 (reset) not allowed
#define CAM_REG_WRITE_DENY 0x3008

#if CONFIG_USE_UDPLIB
#define SEND_CFG(buf, len) send_udp_cfg((buf), (len))
#else
#define SEND_CFG(buf, len) ((void)0)
#endif

typedef enum {
    CAM_CFG_FRAMESIZE      = 1 << 0,
    CAM_CFG_BRIGHTNESS     = 1 << 1,
    CAM_CFG_CONTRAST       = 1 << 2,
    CAM_CFG_SATURATION     = 1 << 3,
    CAM_CFG_SHARPNESS      = 1 << 4,
    CAM_CFG_DENOISE        = 1 << 5,
    CAM_CFG_QUALITY        = 1 << 6,
    CAM_CFG_GAINCEILING    = 1 << 7,
    CAM_CFG_COLORBAR       = 1 << 8,
    CAM_CFG_WHITEBAL       = 1 << 9,
    CAM_CFG_AWB_GAIN       = 1 << 10,
    CAM_CFG_WB_MODE        = 1 << 11,
    CAM_CFG_EXPOSURE_CTRL  = 1 << 12,
    CAM_CFG_AEC2           = 1 << 13,
    CAM_CFG_AE_LEVEL       = 1 << 14,
    CAM_CFG_AEC_VALUE      = 1 << 15,
    CAM_CFG_GAIN_CTRL      = 1 << 16,
    CAM_CFG_AGC_GAIN       = 1 << 17,
    CAM_CFG_HMIRROR        = 1 << 18,
    CAM_CFG_VFLIP          = 1 << 19,
    CAM_CFG_DCW            = 1 << 20,
    CAM_CFG_BPC            = 1 << 21,
    CAM_CFG_WPC            = 1 << 22,
    CAM_CFG_RAW_GMA        = 1 << 23,
    CAM_CFG_LENC           = 1 << 24,
    CAM_CFG_SPECIAL_EFFECT = 1 << 25,
    CAM_CFG_XCLK           = 1 << 26,
    CAM_CFG_PIXFORMAT      = 1 << 27,
} camera_config_field_t;

typedef struct {
    uint32_t field_mask;   // sta -> ESP: changes mask; ESP -> sta: failed mask
    framesize_t framesize;
    int brightness;
    int contrast;
    int saturation;
    int sharpness;
    int denoise;
    int quality;
    uint16_t gainceiling;
    int colorbar;
    int whitebal;
    int awb_gain;
    int wb_mode;
    int exposure_ctrl;
    int aec2;
    int ae_level;
    int aec_value;
    int gain_ctrl;
    int agc_gain;
    int hmirror;
    int vflip;
    int dcw;
    int bpc;
    int wpc;
    int raw_gma;
    int lenc;
    int special_effect;
    int xclk;              // kHz
    pixformat_t pix_format;
} camera_config_t_app;

static const char* TAG = "camera_library";

typedef enum {
    CONFIG_SEND_TYPE_UNKNWON = 0,
    CONFIG_SEND_TYPE_GET     = 1,
    CONFIG_SEND_TYPE_SET     = 2,
    CONFIG_SEND_TYPE_CAMCFG  = 3,

    CONFIG_SEND_TYPE_MAX
} config_send_type_t;


static camera_config_t camera_config = {
    .pin_pwdn  = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    //with encryption, it reduces the frequency available in DMA
    .xclk_freq_hz = 24000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .fb_location = CAMERA_FB_IN_PSRAM,

    #ifdef CONFIG_CAM_FORMAT_JPEG
    .pixel_format = PIXFORMAT_JPEG, //YUV422,GRAYSCALE,RGB565,JPEG
    #elif defined(CONFIG_CAM_FORMAT_YUV)
    .pixel_format = PIXFORMAT_YUV422,
    #elif defined(CONFIG_CAM_FORMAT_RGB)
    .pixel_format = PIXFORMAT_RGB565,
    #endif

    #ifdef CONFIG_CAM_RES_QQVGA
    .frame_size = FRAMESIZE_QQVGA, //QQVGA-UXGA, For ESP32, do not use sizes above QVGA when not JPEG. The performance of the ESP32-S series has improved a lot, but JPEG mode always gives better frame rates.
    #elif defined(CONFIG_CAM_RES_QVGA)
    .frame_size = FRAMESIZE_QVGA,
    #elif defined(CONFIG_CAM_RES_VGA)
    .frame_size = FRAMESIZE_VGA,
    #endif

    .jpeg_quality = 12, //0-63, for OV series camera sensors, lower number means higher quality
    .fb_count = 2, //When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode.
    .grab_mode = CAMERA_GRAB_LATEST // Sets when buffers should be filled
};

// Raw xclk in KHz from LEDC
static esp_err_t set_xclk_khz(sensor_t *s, uint32_t khz) {
    uint32_t hz = khz * 1000u;

    return (esp_err_t)s->set_xclk(s, camera_config.ledc_timer, hz);
    /*
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_1_BIT,   // same resolution as camera driver
        .timer_num = camera_config.ledc_timer,
        .freq_hz = hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t r = ledc_timer_config(&t);
    if (r == ESP_OK) {
        s->xclk_freq_hz = hz;   // update driver xclk
        log_msg(TAG, "XCLK asked %lu Hz, actual %lu Hz", (unsigned long)hz,
                (unsigned long)ledc_get_freq(LEDC_LOW_SPEED_MODE, camera_config.ledc_timer));
    } else {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "XCLK set failed (%d)", (int)r);
    }
    */
}

#define CAM_PID_OV5640 0x5640

static int set_gainceiling_raw(sensor_t *s, uint16_t v) {
    if (s->id.PID == CAM_PID_OV5640) {
        if (v > 1023 || s->set_reg == NULL) return -1;
        return s->set_reg(s, 0x3A18, 0x03, (v >> 8) & 0x03)
            || s->set_reg(s, 0x3A19, 0xFF, v & 0xFF);
    }
    if (v > GAINCEILING_128X || s->set_gainceiling == NULL) return -1;   // OV2640 : enum 0..6
    return s->set_gainceiling(s, (gainceiling_t)v);
}

static int get_gainceiling_raw(sensor_t *s) {
    if (s->id.PID == CAM_PID_OV5640 && s->get_reg != NULL) {
        int hi = s->get_reg(s, 0x3A18, 0x03), lo = s->get_reg(s, 0x3A19, 0xFF);
        return (hi < 0 || lo < 0) ? -1 : ((hi << 8) | lo);
    }
    return s->status.gainceiling;
}

// ---------------------------------------------------------------------------
// Config camera : STA -> ESP
// ---------------------------------------------------------------------------
static bool deserialize_camera_config(const uint8_t *buffer, size_t len, camera_config_t_app *out) {
    if (buffer == NULL || len < CAMCFG_MIN_SIZE) {
        return false;
    }

    uint16_t aec_value;
    uint16_t xclk_value;
    uint16_t gainceling_value;
    memset(out, 0, sizeof(camera_config_t_app));

    out->field_mask = (uint32_t)buffer[0]
                     | ((uint32_t)buffer[1] << 8)
                     | ((uint32_t)buffer[2] << 16)
                     | ((uint32_t)buffer[3] << 24);

    out->framesize      = (framesize_t)buffer[CAMCFG_OFF_FRAMESIZE];
    out->brightness     = (int8_t)buffer[CAMCFG_OFF_BRIGHTNESS];
    out->contrast       = (int8_t)buffer[CAMCFG_OFF_CONTRAST];
    out->saturation     = (int8_t)buffer[CAMCFG_OFF_SATURATION];
    out->sharpness      = (int8_t)buffer[CAMCFG_OFF_SHARPNESS];
    out->denoise        = buffer[CAMCFG_OFF_DENOISE];
    out->quality        = buffer[CAMCFG_OFF_QUALITY];
    memcpy(&gainceling_value, &buffer[CAMCFG_OFF_GAINCEILING], sizeof(uint16_t));
    out->gainceiling    = gainceling_value;
    out->colorbar       = buffer[CAMCFG_OFF_COLORBAR];
    out->whitebal       = buffer[CAMCFG_OFF_WHITEBAL];
    out->awb_gain       = buffer[CAMCFG_OFF_AWB_GAIN];
    out->wb_mode        = buffer[CAMCFG_OFF_WB_MODE];
    out->exposure_ctrl  = buffer[CAMCFG_OFF_EXPOSURE_CTRL];
    out->aec2           = buffer[CAMCFG_OFF_AEC2];
    out->ae_level       = (int8_t)buffer[CAMCFG_OFF_AE_LEVEL];
    memcpy(&aec_value, &buffer[CAMCFG_OFF_AEC_VALUE], sizeof(uint16_t));
    out->aec_value      = aec_value;
    out->gain_ctrl      = buffer[CAMCFG_OFF_GAIN_CTRL];
    out->agc_gain       = buffer[CAMCFG_OFF_AGC_GAIN];
    out->hmirror        = buffer[CAMCFG_OFF_HMIRROR];
    out->vflip          = buffer[CAMCFG_OFF_VFLIP];
    out->dcw            = buffer[CAMCFG_OFF_DCW];
    out->bpc            = buffer[CAMCFG_OFF_BPC];
    out->wpc            = buffer[CAMCFG_OFF_WPC];
    out->raw_gma        = buffer[CAMCFG_OFF_RAW_GMA];
    out->lenc           = buffer[CAMCFG_OFF_LENC];
    out->special_effect = buffer[CAMCFG_OFF_SPECIAL_EFFECT];
    memcpy(&xclk_value, &buffer[CAMCFG_OFF_XCLK], sizeof(uint16_t));
    out->xclk           = xclk_value;

    if (len > CAMCFG_OFF_PIXFORMAT) {
        out->pix_format = (pixformat_t)buffer[CAMCFG_OFF_PIXFORMAT];
    } else {
        out->field_mask &= ~(uint32_t)CAM_CFG_PIXFORMAT;
    }
    return true;
}

// Apply a field function safely & update failed mask 
#define APPLY(bit, fn, ...) do { \
    if (cfg.field_mask & (bit)) { \
        if ((fn) == NULL || (fn)(__VA_ARGS__) != 0) { failed |= (bit); } \
    } \
} while (0)


static void serialize_camera_config(const camera_config_t_app *cfg, uint8_t *buf) {
    buf[0] = CONFIG_SEND_TYPE_CAMCFG;
    uint8_t *p = buf + 1;
    memset(p, 0, CAMCFG_PAYLOAD_SIZE);

    uint16_t aec_value;
    uint16_t xclk;

    memcpy(&p[CAMCFG_OFF_MASK], &cfg->field_mask, sizeof(uint32_t));
    p[CAMCFG_OFF_FRAMESIZE]      = (uint8_t)cfg->framesize;
    p[CAMCFG_OFF_BRIGHTNESS]     = (uint8_t)(int8_t)cfg->brightness;
    p[CAMCFG_OFF_CONTRAST]       = (uint8_t)(int8_t)cfg->contrast;
    p[CAMCFG_OFF_SATURATION]     = (uint8_t)(int8_t)cfg->saturation;
    p[CAMCFG_OFF_SHARPNESS]      = (uint8_t)(int8_t)cfg->sharpness;
    p[CAMCFG_OFF_DENOISE]        = (uint8_t)cfg->denoise;
    p[CAMCFG_OFF_QUALITY]        = (uint8_t)cfg->quality;
    memcpy(&p[CAMCFG_OFF_GAINCEILING], &cfg->gainceiling, sizeof(uint16_t));
    p[CAMCFG_OFF_COLORBAR]       = cfg->colorbar ? 1 : 0;
    p[CAMCFG_OFF_WHITEBAL]       = cfg->whitebal ? 1 : 0;
    p[CAMCFG_OFF_AWB_GAIN]       = cfg->awb_gain ? 1 : 0;
    p[CAMCFG_OFF_WB_MODE]        = (uint8_t)cfg->wb_mode;
    p[CAMCFG_OFF_EXPOSURE_CTRL]  = cfg->exposure_ctrl ? 1 : 0;
    p[CAMCFG_OFF_AEC2]           = cfg->aec2 ? 1 : 0;
    p[CAMCFG_OFF_AE_LEVEL]       = (uint8_t)(int8_t)cfg->ae_level;
    aec_value = (uint16_t)cfg->aec_value;
    memcpy(&p[CAMCFG_OFF_AEC_VALUE], &aec_value, sizeof(uint16_t));
    p[CAMCFG_OFF_GAIN_CTRL]      = cfg->gain_ctrl ? 1 : 0;
    p[CAMCFG_OFF_AGC_GAIN]       = (uint8_t)cfg->agc_gain;
    p[CAMCFG_OFF_HMIRROR]        = cfg->hmirror ? 1 : 0;
    p[CAMCFG_OFF_VFLIP]          = cfg->vflip ? 1 : 0;
    p[CAMCFG_OFF_DCW]            = cfg->dcw ? 1 : 0;
    p[CAMCFG_OFF_BPC]            = cfg->bpc ? 1 : 0;
    p[CAMCFG_OFF_WPC]            = cfg->wpc ? 1 : 0;
    p[CAMCFG_OFF_RAW_GMA]        = cfg->raw_gma ? 1 : 0;
    p[CAMCFG_OFF_LENC]           = cfg->lenc ? 1 : 0;
    p[CAMCFG_OFF_SPECIAL_EFFECT] = (uint8_t)cfg->special_effect;
    xclk = (uint16_t)cfg->xclk;
    memcpy(&p[CAMCFG_OFF_XCLK], &xclk, sizeof(uint16_t));
    p[CAMCFG_OFF_PIXFORMAT]      = (uint8_t)cfg->pix_format;
}

// Send Camera state ESP -> STA
static esp_err_t send_camera_state(uint32_t failed_mask) {
    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Camera not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    camera_config_t_app c;
    memset(&c, 0, sizeof(c));
    c.field_mask     = failed_mask;
    c.framesize      = s->status.framesize;
    c.brightness     = s->status.brightness;
    c.contrast       = s->status.contrast;
    c.saturation     = s->status.saturation;
    c.sharpness      = s->status.sharpness;
    c.denoise        = s->status.denoise;
    c.quality        = s->status.quality;
    c.gainceiling    = get_gainceiling_raw(s);
    c.colorbar       = s->status.colorbar;
    c.whitebal       = s->status.awb;
    c.awb_gain       = s->status.awb_gain;
    c.wb_mode        = s->status.wb_mode;
    c.exposure_ctrl  = s->status.aec;
    c.aec2           = s->status.aec2;
    c.ae_level       = s->status.ae_level;
    c.aec_value      = s->status.aec_value;
    c.gain_ctrl      = s->status.agc;
    c.agc_gain       = s->status.agc_gain;
    c.hmirror        = s->status.hmirror;
    c.vflip          = s->status.vflip;
    c.dcw            = s->status.dcw;
    c.bpc            = s->status.bpc;
    c.wpc            = s->status.wpc;
    c.raw_gma        = s->status.raw_gma;
    c.lenc           = s->status.lenc;
    c.special_effect = s->status.special_effect;

    // Raw xclk
    //uint32_t hz = ledc_get_freq(LEDC_LOW_SPEED_MODE, camera_config.ledc_timer);
    //log_msg(TAG, "xclk value raw: %u", hz);
    uint32_t hz = 0;
    if (hz == 0) {
        hz = (uint32_t)s->xclk_freq_hz;
    }
    c.xclk       = (int)(hz / 1000u);
    c.pix_format = s->pixformat;

    uint8_t buf[CAMCFG_FRAME_SIZE];
    serialize_camera_config(&c, buf);
    SEND_CFG(buf, sizeof(buf));
    return ESP_OK;
}

// Response from an ask of the current config (ESP -> STA)
esp_err_t get_config_cam(void) {
    return send_camera_state(0);
}

// Apply config sent STA -> ESP, then ack ESP -> STA
void apply_camera_config(const uint8_t *buffer, size_t len) {
    camera_config_t_app cfg;

    if (!deserialize_camera_config(buffer, len, &cfg)) {
        log_msg_lvl(ESP_LOG_WARN, TAG, "Camera config frame too short (%u < %u)",
                    (unsigned)len, (unsigned)CAMCFG_MIN_SIZE);
        return;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Camera not initialized");
        return;
    }

    uint32_t failed = 0;

    // Safety for buffer size overflow at init
    if (cfg.field_mask & CAM_CFG_PIXFORMAT) {
        if (cfg.pix_format != s->pixformat) {
            log_msg_lvl(ESP_LOG_WARN, TAG, "Runtime pixformat change refused (needs deinit/init)");
            failed |= CAM_CFG_PIXFORMAT;
        }
    }

    if (cfg.field_mask & CAM_CFG_FRAMESIZE) {
        if ((int)cfg.framesize > (int)CAM_MAX_FRAMESIZE) {
            log_msg_lvl(ESP_LOG_WARN, TAG, "Framesize %d refused (max %d)",
                        (int)cfg.framesize, (int)CAM_MAX_FRAMESIZE);
            failed |= CAM_CFG_FRAMESIZE;
        } else {
            APPLY(CAM_CFG_FRAMESIZE, s->set_framesize, s, cfg.framesize);
        }
    }

    APPLY(CAM_CFG_BRIGHTNESS,    s->set_brightness,    s, cfg.brightness);
    APPLY(CAM_CFG_CONTRAST,      s->set_contrast,      s, cfg.contrast);
    APPLY(CAM_CFG_SATURATION,    s->set_saturation,    s, cfg.saturation);
    APPLY(CAM_CFG_SHARPNESS,     s->set_sharpness,     s, cfg.sharpness);
    APPLY(CAM_CFG_DENOISE,       s->set_denoise,       s, cfg.denoise);
    APPLY(CAM_CFG_QUALITY,       s->set_quality,       s, cfg.quality);
    APPLY(CAM_CFG_GAINCEILING,   set_gainceiling_raw,  s, cfg.gainceiling);
    APPLY(CAM_CFG_COLORBAR,      s->set_colorbar,      s, cfg.colorbar);
    APPLY(CAM_CFG_WHITEBAL,      s->set_whitebal,      s, cfg.whitebal);
    APPLY(CAM_CFG_AWB_GAIN,      s->set_awb_gain,      s, cfg.awb_gain);
    APPLY(CAM_CFG_WB_MODE,       s->set_wb_mode,       s, cfg.wb_mode);
    APPLY(CAM_CFG_EXPOSURE_CTRL, s->set_exposure_ctrl, s, cfg.exposure_ctrl);
    APPLY(CAM_CFG_AEC2,          s->set_aec2,          s, cfg.aec2);
    APPLY(CAM_CFG_AE_LEVEL,      s->set_ae_level,      s, cfg.ae_level);
    APPLY(CAM_CFG_AEC_VALUE,     s->set_aec_value,     s, cfg.aec_value);
    APPLY(CAM_CFG_GAIN_CTRL,     s->set_gain_ctrl,     s, cfg.gain_ctrl);
    APPLY(CAM_CFG_AGC_GAIN,      s->set_agc_gain,      s, cfg.agc_gain);
    APPLY(CAM_CFG_HMIRROR,       s->set_hmirror,       s, cfg.hmirror);
    APPLY(CAM_CFG_VFLIP,         s->set_vflip,         s, cfg.vflip);
    APPLY(CAM_CFG_DCW,           s->set_dcw,           s, cfg.dcw);
    APPLY(CAM_CFG_BPC,           s->set_bpc,           s, cfg.bpc);
    APPLY(CAM_CFG_WPC,           s->set_wpc,           s, cfg.wpc);
    APPLY(CAM_CFG_RAW_GMA,       s->set_raw_gma,       s, cfg.raw_gma);
    APPLY(CAM_CFG_LENC,          s->set_lenc,          s, cfg.lenc);
    APPLY(CAM_CFG_SPECIAL_EFFECT, s->set_special_effect, s, cfg.special_effect);

    /*
    if (cfg.field_mask & CAM_CFG_XCLK) {
        if (cfg.xclk < CAM_XCLK_MIN_KHZ || cfg.xclk > CAM_XCLK_MAX_KHZ) {
            log_msg_lvl(ESP_LOG_WARN, TAG, "XCLK %d kHz out of range", cfg.xclk);
            failed |= CAM_CFG_XCLK;
        } else if (set_xclk_khz(s, (uint32_t)cfg.xclk) != ESP_OK) {
            failed |= CAM_CFG_XCLK;
        }
    }
    //s->set_pll
    */

    log_msg(TAG, "Config applied, mask=0x%08lx failed=0x%08lx",
            (unsigned long)cfg.field_mask, (unsigned long)failed);

    // ACK: ESP -> STA of new state
    send_camera_state(failed);
}
#undef APPLY

// ---------------------------------------------------------------------------
// Read / Write of raw registers
// ---------------------------------------------------------------------------
#define GET_REG_RECV_SIZE       3
#define GET_REG_SEND_FRAME_SIZE 8
#define SET_REG_RECV_SIZE       4
#define SET_REG_SEND_FRAME_SIZE 12

typedef struct {
    int32_t value;
    uint16_t reg;
    uint8_t mask;
} get_reg_send_t;

static void serialize_get_reg_send(const get_reg_send_t *msg, uint8_t *buf) {
    buf[0] = CONFIG_SEND_TYPE_GET;
    memcpy(&buf[1], &msg->value, sizeof(int32_t));
    memcpy(&buf[5], &msg->reg, sizeof(uint16_t));
    buf[7] = msg->mask;
}

typedef struct {
    uint16_t reg;
    uint8_t mask;
} get_reg_recv_t;

static bool deserialize_get_reg_recv(get_reg_recv_t *msg, const uint8_t *buf, size_t len) {
    if (buf == NULL || len < GET_REG_RECV_SIZE) {
        return false;
    }
    memcpy(&msg->reg, &buf[0], sizeof(uint16_t));
    msg->mask = buf[2];
    return true;
}

// Get register value STA -> ESP then ack ESP -> STA
esp_err_t get_reg_cam(const uint8_t *buffer, size_t len) {
    get_reg_recv_t recv_msg;
    if (!deserialize_get_reg_recv(&recv_msg, buffer, len)) {
        log_msg_lvl(ESP_LOG_WARN, TAG, "get_reg frame too short (%u)", (unsigned)len);
        return ESP_ERR_INVALID_SIZE;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Camera not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    get_reg_send_t msg = { .value = -1, .reg = recv_msg.reg, .mask = recv_msg.mask };
    if (s->get_reg != NULL) {
        msg.value = s->get_reg(s, recv_msg.reg, recv_msg.mask);
    }

    uint8_t resp_buf[GET_REG_SEND_FRAME_SIZE];
    serialize_get_reg_send(&msg, resp_buf);
    SEND_CFG(resp_buf, sizeof(resp_buf));
    return ESP_OK;
}

typedef struct {
    int32_t ret;
    uint16_t reg;
    uint8_t mask;
    int32_t newer;
} set_reg_send_t;

static void serialize_set_reg_send(const set_reg_send_t *msg, uint8_t *buf) {
    buf[0] = CONFIG_SEND_TYPE_SET;
    memcpy(&buf[1], &msg->ret, sizeof(int32_t));
    memcpy(&buf[5], &msg->reg, sizeof(uint16_t));
    buf[7] = msg->mask;
    memcpy(&buf[8], &msg->newer, sizeof(int32_t));
}

typedef struct {
    uint16_t reg;
    uint8_t mask;
    uint8_t value;
} set_reg_recv_t;

static bool deserialize_set_reg_recv(set_reg_recv_t *msg, const uint8_t *buf, size_t len) {
    if (buf == NULL || len < SET_REG_RECV_SIZE) {
        return false;
    }
    memcpy(&msg->reg, &buf[0], sizeof(uint16_t));
    msg->mask = buf[2];
    msg->value = buf[3];
    return true;
}

// Set register STA -> ESP then ack ESP -> STA
esp_err_t set_reg_cam(const uint8_t *buffer, size_t len) {
    set_reg_recv_t recv_msg;
    if (!deserialize_set_reg_recv(&recv_msg, buffer, len)) {
        log_msg_lvl(ESP_LOG_WARN, TAG, "set_reg frame too short (%u)", (unsigned)len);
        return ESP_ERR_INVALID_SIZE;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Camera not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    set_reg_send_t msg = { .ret = -1, .reg = recv_msg.reg, .mask = recv_msg.mask, .newer = -1 };
    if (recv_msg.reg == CAM_REG_WRITE_DENY) {
        log_msg_lvl(ESP_LOG_WARN, TAG, "Write to reg 0x%04x refused", (unsigned)recv_msg.reg);
    } else if (s->set_reg != NULL) {
        msg.ret = s->set_reg(s, recv_msg.reg, recv_msg.mask, recv_msg.value);
        if (s->get_reg != NULL) {
            msg.newer = s->get_reg(s, recv_msg.reg, recv_msg.mask);
        }
    }

    uint8_t resp_buf[SET_REG_SEND_FRAME_SIZE];
    serialize_set_reg_send(&msg, resp_buf);
    SEND_CFG(resp_buf, sizeof(resp_buf));
    return ESP_OK;
}


static void jpg_stream_udp(void *param){
    camera_fb_t * fb = NULL;
    static int frame_count = 0;
    static int64_t start = 0;

    vTaskDelay(pdMS_TO_TICKS(1000)); 

    while(true){

        if (atomic_load(&ota_lock)) {
            break;
        }
        fb = esp_camera_fb_get();
        if (!fb) {
            log_msg_lvl(ESP_LOG_ERROR, TAG, "Camera capture failed");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

#if CONFIG_USE_UDPLIB
        if (fb->buf != NULL && fb->len > 0) {
            send_udp_jpeg(fb->buf, fb->len);
        }
#endif

        esp_camera_fb_return(fb);
        
        #if CONFIG_FPS_COUNT
        frame_count++;
        if (frame_count == 1) start = esp_timer_get_time();
        if (frame_count % 30 == 0) {
            float fps = 30 * 1000000.0f / (esp_timer_get_time() - start);
            start = esp_timer_get_time();
            log_msg(TAG, "FPS: %.1f, free heap: %lu", fps, esp_get_free_heap_size());
        }
        #endif
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    vTaskDelete(NULL);
}

esp_err_t camera_init(){

    //initialize the camera
    log_msg(TAG, "Trying camera init..");
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Camera Init Failed");
        return err;
    }

    BaseType_t res = xTaskCreate(jpg_stream_udp, "jpg_stream_udp", 8192, NULL, 4, NULL);
    if (res != pdPASS) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%d) creating jpg stream UDP task", res);
    }

    log_msg(TAG, "Camera initialized");

    return ESP_OK;
}