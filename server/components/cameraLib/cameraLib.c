#include "cameraLib.h"
#include "udpLib.h"
#include "logLib.h"
#include "udpLib.h"
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
    .xclk_freq_hz = 17500000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_RGB565, //YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_QVGA, //QQVGA-UXGA, For ESP32, do not use sizes above QVGA when not JPEG. The performance of the ESP32-S series has improved a lot, but JPEG mode always gives better frame rates.

    .jpeg_quality = 15, //0-63, for OV series camera sensors, lower number means higher quality
    .fb_count = 2, //When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode.
    .grab_mode = CAMERA_GRAB_LATEST // Sets when buffers should be filled
};


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

        uint8_t * out_buf = NULL;
        size_t out_len = 0;

        bool converted = frame2jpg(fb, 10, &out_buf, &out_len);

        esp_camera_fb_return(fb);

        if (converted) {
            udp_msg_vid_t msg;
            msg.data = out_buf;
            msg.len = out_len;

            //log_msg(TAG, "Image converted, len: %u", out_len);
            send_udp_jpeg(&msg);
        } else {
            if (out_buf != NULL) free(out_buf);
        }
        
        frame_count++;
        if (frame_count == 1) start = esp_timer_get_time();
        if (frame_count % 30 == 0) {
            float fps = 30 * 1000000.0f / (esp_timer_get_time() - start);
            start = esp_timer_get_time();
            log_msg(TAG, "FPS: %.1f, free heap: %lu", fps, esp_get_free_heap_size());
        }
        //vTaskDelay(pdMS_TO_TICKS(50));
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