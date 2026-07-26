#include "cameraLib.h"

#if CONFIG_USE_UDPLIB
#include "udpLib.h"
#endif

#include "logLib.h"
#include "esp_camera.h"
#include "esp_timer.h"

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
#define CAMCFG_OFF_GAINCEILING     11  // u8
#define CAMCFG_OFF_COLORBAR        12  // u8
#define CAMCFG_OFF_WHITEBAL        13  // u8
#define CAMCFG_OFF_AWB_GAIN        14  // u8
#define CAMCFG_OFF_WB_MODE         15  // u8
#define CAMCFG_OFF_EXPOSURE_CTRL   16  // u8
#define CAMCFG_OFF_AEC2            17  // u8
#define CAMCFG_OFF_AE_LEVEL        18  // i8
#define CAMCFG_OFF_AEC_VALUE       19  // u16, 2 bytes
#define CAMCFG_OFF_GAIN_CTRL       21  // u8
#define CAMCFG_OFF_AGC_GAIN        22  // u8
#define CAMCFG_OFF_HMIRROR         23  // u8
#define CAMCFG_OFF_VFLIP           24  // u8
#define CAMCFG_OFF_DCW             25  // u8
#define CAMCFG_OFF_BPC             26  // u8
#define CAMCFG_OFF_WPC             27  // u8
#define CAMCFG_OFF_RAW_GMA         28  // u8
#define CAMCFG_OFF_LENC            29  // u8
#define CAMCFG_OFF_SPECIAL_EFFECT  30  // u8

// Guard contre les fonctions non implémentées par certains drivers de capteur (varie selon OV2640/OV2660/OV3660)
#define CALL_IF(fn, ...) do { if ((fn) != NULL) { (fn)(__VA_ARGS__); } } while (0)

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
} camera_config_field_t;

typedef struct {
    uint32_t field_mask;
    framesize_t framesize;
    int brightness;
    int contrast;
    int saturation;
    int sharpness;
    int denoise;
    int quality;
    gainceiling_t gainceiling;
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
} camera_config_t_app;

static const char* TAG = "camera_library";


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
    //17.5Mhz is the maximum available for the camera, without Wifi, in RGB565
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


    .jpeg_quality = 15, //0-63, for OV series camera sensors, lower number means higher quality
    .fb_count = 2, //When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode.
    .grab_mode = CAMERA_GRAB_LATEST // Sets when buffers should be filled
};

static void deserialize_camera_config(const uint8_t *buffer, size_t len, camera_config_t_app *out) {

    uint16_t aec_value;
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
    out->gainceiling    = (gainceiling_t)buffer[CAMCFG_OFF_GAINCEILING];
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

}

void apply_camera_config(const uint8_t *buffer, size_t len) {
    camera_config_t_app cfg;

    deserialize_camera_config(buffer, len, &cfg);

    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Camera not initialized");
        return;
    }

    if (cfg.field_mask & CAM_CFG_FRAMESIZE)      CALL_IF(s->set_framesize, s, cfg.framesize);
    if (cfg.field_mask & CAM_CFG_BRIGHTNESS)     CALL_IF(s->set_brightness, s, cfg.brightness);
    if (cfg.field_mask & CAM_CFG_CONTRAST)       CALL_IF(s->set_contrast, s, cfg.contrast);
    if (cfg.field_mask & CAM_CFG_SATURATION)     CALL_IF(s->set_saturation, s, cfg.saturation);
    if (cfg.field_mask & CAM_CFG_SHARPNESS)      CALL_IF(s->set_sharpness, s, cfg.sharpness);
    if (cfg.field_mask & CAM_CFG_DENOISE)        CALL_IF(s->set_denoise, s, cfg.denoise);
    if (cfg.field_mask & CAM_CFG_QUALITY)        CALL_IF(s->set_quality, s, cfg.quality);
    if (cfg.field_mask & CAM_CFG_GAINCEILING)    CALL_IF(s->set_gainceiling, s, cfg.gainceiling);
    if (cfg.field_mask & CAM_CFG_COLORBAR)       CALL_IF(s->set_colorbar, s, cfg.colorbar);
    if (cfg.field_mask & CAM_CFG_WHITEBAL)       CALL_IF(s->set_whitebal, s, cfg.whitebal);
    if (cfg.field_mask & CAM_CFG_AWB_GAIN)       CALL_IF(s->set_awb_gain, s, cfg.awb_gain);
    if (cfg.field_mask & CAM_CFG_WB_MODE)        CALL_IF(s->set_wb_mode, s, cfg.wb_mode);
    if (cfg.field_mask & CAM_CFG_EXPOSURE_CTRL)  CALL_IF(s->set_exposure_ctrl, s, cfg.exposure_ctrl);
    if (cfg.field_mask & CAM_CFG_AEC2)           CALL_IF(s->set_aec2, s, cfg.aec2);
    if (cfg.field_mask & CAM_CFG_AE_LEVEL)       CALL_IF(s->set_ae_level, s, cfg.ae_level);
    if (cfg.field_mask & CAM_CFG_AEC_VALUE)      CALL_IF(s->set_aec_value, s, cfg.aec_value);
    if (cfg.field_mask & CAM_CFG_GAIN_CTRL)      CALL_IF(s->set_gain_ctrl, s, cfg.gain_ctrl);
    if (cfg.field_mask & CAM_CFG_AGC_GAIN)       CALL_IF(s->set_agc_gain, s, cfg.agc_gain);
    if (cfg.field_mask & CAM_CFG_HMIRROR)        CALL_IF(s->set_hmirror, s, cfg.hmirror);
    if (cfg.field_mask & CAM_CFG_VFLIP)          CALL_IF(s->set_vflip, s, cfg.vflip);
    if (cfg.field_mask & CAM_CFG_DCW)            CALL_IF(s->set_dcw, s, cfg.dcw);
    if (cfg.field_mask & CAM_CFG_BPC)            CALL_IF(s->set_bpc, s, cfg.bpc);
    if (cfg.field_mask & CAM_CFG_WPC)            CALL_IF(s->set_wpc, s, cfg.wpc);
    if (cfg.field_mask & CAM_CFG_RAW_GMA)        CALL_IF(s->set_raw_gma, s, cfg.raw_gma);
    if (cfg.field_mask & CAM_CFG_LENC)           CALL_IF(s->set_lenc, s, cfg.lenc);
    if (cfg.field_mask & CAM_CFG_SPECIAL_EFFECT) CALL_IF(s->set_special_effect, s, cfg.special_effect);

    log_msg(TAG, "Config applied, mask=0x%08lx", (unsigned long)cfg.field_mask);
}


static void jpg_stream_udp(void *param){
    camera_fb_t * fb = NULL;
    static int frame_count = 0;
    static int64_t start = 0;

    vTaskDelay(pdMS_TO_TICKS(1000)); 


    while(true){
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