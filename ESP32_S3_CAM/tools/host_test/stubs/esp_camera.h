#pragma once
#include <cstddef>
#include <cstdint>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NOT_FOUND 0x105
inline const char* esp_err_to_name(esp_err_t) { return "ERR"; }
typedef enum { PIXFORMAT_RGB565, PIXFORMAT_YUV422, PIXFORMAT_YUV420, PIXFORMAT_GRAYSCALE, PIXFORMAT_JPEG, PIXFORMAT_RGB888 } pixformat_t;
typedef enum { FRAMESIZE_96X96, FRAMESIZE_QQVGA, FRAMESIZE_128X128, FRAMESIZE_QCIF, FRAMESIZE_HQVGA, FRAMESIZE_240X240, FRAMESIZE_QVGA } framesize_t;
typedef enum { GAINCEILING_2X, GAINCEILING_4X, GAINCEILING_8X } gainceiling_t;
typedef enum { LEDC_TIMER_0 } ledc_timer_t;
typedef enum { LEDC_CHANNEL_0 } ledc_channel_t;
typedef enum { CAMERA_FB_IN_PSRAM, CAMERA_FB_IN_DRAM } camera_fb_location_t;
typedef enum { CAMERA_GRAB_WHEN_EMPTY, CAMERA_GRAB_LATEST } camera_grab_mode_t;
enum { OV3660_PID = 0x3660 };
typedef struct { const uint16_t width; const uint16_t height; const int aspect_ratio; } resolution_info_t;
extern const resolution_info_t resolution[];
typedef struct { int pin_pwdn, pin_reset, pin_xclk; union { int pin_sccb_sda; int pin_sscb_sda; }; union { int pin_sccb_scl; int pin_sscb_scl; };
  int pin_d7, pin_d6, pin_d5, pin_d4, pin_d3, pin_d2, pin_d1, pin_d0, pin_vsync, pin_href, pin_pclk; int xclk_freq_hz;
  ledc_timer_t ledc_timer; ledc_channel_t ledc_channel; pixformat_t pixel_format; framesize_t frame_size; int jpeg_quality; size_t fb_count;
  camera_fb_location_t fb_location; camera_grab_mode_t grab_mode; int sccb_i2c_port; } camera_config_t;
typedef struct { uint8_t * buf; size_t len; size_t width; size_t height; pixformat_t format; } camera_fb_t;
typedef struct { uint8_t MIDH; uint8_t MIDL; uint16_t PID; uint8_t VER; } sensor_id_t;
typedef struct { int model; int pid; int sccb_addr; const char* name; } camera_sensor_info_t;
typedef struct _sensor sensor_t;
struct _sensor { sensor_id_t id;
  int (*set_vflip)(sensor_t*, int); int (*set_hmirror)(sensor_t*, int); int (*set_brightness)(sensor_t*, int); int (*set_contrast)(sensor_t*, int);
  int (*set_saturation)(sensor_t*, int); int (*set_whitebal)(sensor_t*, int); int (*set_awb_gain)(sensor_t*, int); int (*set_wb_mode)(sensor_t*, int);
  int (*set_exposure_ctrl)(sensor_t*, int); int (*set_aec2)(sensor_t*, int); int (*set_ae_level)(sensor_t*, int); int (*set_gain_ctrl)(sensor_t*, int);
  int (*set_gainceiling)(sensor_t*, gainceiling_t); };
esp_err_t esp_camera_init(const camera_config_t*); esp_err_t esp_camera_deinit(); camera_fb_t* esp_camera_fb_get(); void esp_camera_fb_return(camera_fb_t*);
sensor_t* esp_camera_sensor_get(); camera_sensor_info_t* esp_camera_sensor_get_info(sensor_id_t*);
