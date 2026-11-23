// camera_module.h — Camera OV3660 qua esp32-camera (đi kèm Arduino-ESP32 Core 3.x)
//
// * Khởi tạo một lần trong setup(), tái sử dụng cho mọi lần chụp. Không stream, không web server.
// * Chỉ chụp khi được gọi capture() (do state machine gọi khi cảm biến phát hiện vật).
// * Frame buffer nằm trong PSRAM, fb_count = 1. Giữ frame tới sau inference để gửi ảnh rồi gọi release();
//   sau release() mọi con trỏ tới dữ liệu frame đều KHÔNG còn hợp lệ.
#pragma once

#include <Arduino.h>

#include "esp_camera.h"
#include "image_preprocessing.h"

enum CameraError {
  CAM_OK = 0,
  CAM_ERR_NO_PSRAM,
  CAM_ERR_INIT_FAILED,
  CAM_ERR_SENSOR_NOT_FOUND,
  CAM_ERR_WRONG_SENSOR,
  CAM_ERR_NOT_READY,
  CAM_ERR_CAPTURE_FAILED,
  CAM_ERR_BAD_FORMAT,
  CAM_ERR_BAD_SIZE,
  CAM_ERR_DECODE_FAILED,
  CAM_ERR_NO_MEMORY,
  CAM_ERR_NO_FRAME,
};

class CameraModule {
 public:
  bool begin();
  bool reinit();
  void end();
  bool isReady() const { return ready_; }

  // Bỏ frame cũ rồi chụp frame mới, có retry giới hạn. Thành công → giữ frame tới khi release().
  bool capture(uint32_t* elapsedUs);
  // Ảnh để tiền xử lý. Với JPEG sẽ giải mã sang BGR888 (buffer PSRAM cấp phát một lần).
  bool getImage(ImageView* out, uint32_t* decodeUs);
  const camera_fb_t* frame() const { return fb_; }
  void release();

  CameraError lastError() const { return error_; }
  static const char* errorString(CameraError e);
  uint16_t sensorPid() const { return pid_; }
  const char* sensorName() const { return sensorName_; }
  int frameWidth() const { return width_; }
  int frameHeight() const { return height_; }
  uint32_t consecutiveFailures() const { return consecutiveFailures_; }
  uint32_t reinitCount() const { return reinitCount_; }

 private:
  bool validate(const camera_fb_t* fb);
  void applySensorSettings(sensor_t* s);

  bool ready_ = false;            // driver đã cài + warm-up thành công
  bool driverInstalled_ = false;  // esp_camera_init() đã thành công (cần deinit khi tắt)
  camera_fb_t* fb_ = nullptr;
  uint8_t* decodeBuf_ = nullptr;
  size_t decodeBufSize_ = 0;
  CameraError error_ = CAM_ERR_NOT_READY;
  uint16_t pid_ = 0;
  const char* sensorName_ = "unknown";
  int width_ = 0;
  int height_ = 0;
  uint32_t consecutiveFailures_ = 0;
  uint32_t reinitCount_ = 0;
};
