// camera_module.cpp — xem camera_module.h
#include "camera_module.h"

#include <esp_heap_caps.h>

#include "board_pins.h"
#include "config.h"
#if CAMERA_USE_JPEG
#include "img_converters.h"
#endif

bool CameraModule::begin() {
  if (driverInstalled_) end();
  ready_ = false;
  error_ = CAM_ERR_NOT_READY;
  Serial.println("[CAMERA] Initializing OV3660...");
  Serial.printf("[CAMERA] Pin profile: %s%s\n", BOARD_PROFILE_NAME,
                BOARD_PINOUT_VERIFIED ? "" : "  (CHUA XAC MINH voi schematic - xem board_pins.h)");

  if (!psramFound()) {
    error_ = CAM_ERR_NO_PSRAM;
    Serial.println("[CAMERA] ERROR: PSRAM not available; frame buffer requires PSRAM");
    return false;
  }

  camera_config_t config = {};
  config.pin_pwdn = CAM_PIN_PWDN;
  config.pin_reset = CAM_PIN_RESET;
  config.pin_xclk = CAM_PIN_XCLK;
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;
  config.pin_d0 = CAM_PIN_D0;
  config.pin_d1 = CAM_PIN_D1;
  config.pin_d2 = CAM_PIN_D2;
  config.pin_d3 = CAM_PIN_D3;
  config.pin_d4 = CAM_PIN_D4;
  config.pin_d5 = CAM_PIN_D5;
  config.pin_d6 = CAM_PIN_D6;
  config.pin_d7 = CAM_PIN_D7;
  config.pin_vsync = CAM_PIN_VSYNC;
  config.pin_href = CAM_PIN_HREF;
  config.pin_pclk = CAM_PIN_PCLK;
  config.xclk_freq_hz = CAMERA_XCLK_FREQ_HZ;
  config.ledc_timer = LEDC_TIMER_0;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.pixel_format = CAMERA_USE_JPEG ? PIXFORMAT_JPEG : PIXFORMAT_RGB565;
  config.frame_size = CAMERA_FRAME_SIZE;
  config.jpeg_quality = CAMERA_JPEG_QUALITY;
  config.fb_count = CAMERA_FB_COUNT;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  const esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    error_ = (err == ESP_ERR_NOT_FOUND) ? CAM_ERR_SENSOR_NOT_FOUND : CAM_ERR_INIT_FAILED;
    Serial.printf("[CAMERA] ERROR: esp_camera_init failed: 0x%x (%s)\n", (unsigned)err, esp_err_to_name(err));
    Serial.println("[CAMERA]        -> kiem tra pinout (board_pins.h), cap camera, nguon 3V3, PSRAM = OPI");
    return false;
  }
  driverInstalled_ = true;

  sensor_t* s = esp_camera_sensor_get();
  if (s == nullptr) {
    error_ = CAM_ERR_SENSOR_NOT_FOUND;
    Serial.println("[CAMERA] ERROR: esp_camera_sensor_get() returned NULL");
    end();
    return false;
  }
  pid_ = s->id.PID;
  camera_sensor_info_t* info = esp_camera_sensor_get_info(&s->id);
  sensorName_ = (info != nullptr && info->name != nullptr) ? info->name : "unknown";
  Serial.printf("[CAMERA] Sensor detected: %s (PID 0x%04X)\n", sensorName_, (unsigned)pid_);
  if (pid_ != OV3660_PID) {
#if CAMERA_REQUIRE_OV3660
    error_ = CAM_ERR_WRONG_SENSOR;
    Serial.println("[CAMERA] ERROR: sensor is not OV3660 (PID 0x3660). Dat CAMERA_REQUIRE_OV3660 = 0 neu dung cam bien khac.");
    end();
    return false;
#else
    Serial.println("[CAMERA] WARNING: sensor is not OV3660; continuing because CAMERA_REQUIRE_OV3660 = 0");
#endif
  }

  applySensorSettings(s);
  width_ = resolution[CAMERA_FRAME_SIZE].width;
  height_ = resolution[CAMERA_FRAME_SIZE].height;
  Serial.printf("[CAMERA] Frame %dx%d, %s, fb_count=%d in PSRAM, grab=WHEN_EMPTY, XCLK=%lu Hz\n", width_, height_,
                CAMERA_USE_JPEG ? "JPEG" : "RGB565", CAMERA_FB_COUNT, (unsigned long)CAMERA_XCLK_FREQ_HZ);

#if CAMERA_USE_JPEG
  const size_t need = static_cast<size_t>(width_) * height_ * 3;
  if (decodeBuf_ == nullptr || decodeBufSize_ < need) {
    if (decodeBuf_ != nullptr) heap_caps_free(decodeBuf_);
    decodeBuf_ = static_cast<uint8_t*>(heap_caps_malloc(need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    decodeBufSize_ = decodeBuf_ ? need : 0;
  }
  if (decodeBuf_ == nullptr) {
    error_ = CAM_ERR_NO_MEMORY;
    Serial.printf("[CAMERA] ERROR: cannot allocate %u B JPEG decode buffer in PSRAM\n", (unsigned)need);
    end();
    return false;
  }
#endif

  // Warm-up: bỏ vài frame đầu để AEC/AWB ổn định, rồi kiểm tra một frame hợp lệ.
  for (int i = 0; i < CAMERA_WARMUP_FRAMES; ++i) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb != nullptr) esp_camera_fb_return(fb);
  }
  ready_ = true;
  uint32_t us = 0;
  if (!capture(&us)) {
    ready_ = false;
    Serial.printf("[CAMERA] ERROR: warm-up capture failed (%s)\n", errorString(error_));
    end();
    return false;
  }
  Serial.printf("[CAMERA] Test frame OK: %ux%u, len=%u B, %.1f ms\n", (unsigned)fb_->width, (unsigned)fb_->height,
                (unsigned)fb_->len, us / 1000.0f);
  release();
  error_ = CAM_OK;
  Serial.println("[CAMERA] Camera ready");
  return true;
}

void CameraModule::applySensorSettings(sensor_t* s) {
#define CAM_APPLY(fn, value)                                                                  \
  do {                                                                                        \
    if (s->fn != nullptr) {                                                                   \
      const int r_ = s->fn(s, (value));                                                       \
      if (r_ != 0) Serial.printf("[CAMERA] WARN: " #fn "(%d) returned %d\n", (int)(value), r_); \
    } else {                                                                                  \
      Serial.println("[CAMERA] WARN: sensor driver has no " #fn);                             \
    }                                                                                         \
  } while (0)

  CAM_APPLY(set_vflip, CAM_VFLIP);
  CAM_APPLY(set_hmirror, CAM_HMIRROR);
  CAM_APPLY(set_brightness, CAM_BRIGHTNESS);
  CAM_APPLY(set_contrast, CAM_CONTRAST);
  CAM_APPLY(set_saturation, CAM_SATURATION);
  CAM_APPLY(set_whitebal, CAM_AUTO_WHITE_BALANCE);
  CAM_APPLY(set_awb_gain, CAM_AWB_GAIN);
  CAM_APPLY(set_wb_mode, CAM_WB_MODE);
  CAM_APPLY(set_exposure_ctrl, CAM_AUTO_EXPOSURE);
  CAM_APPLY(set_aec2, CAM_AEC2);
  CAM_APPLY(set_ae_level, CAM_AE_LEVEL);
  CAM_APPLY(set_gain_ctrl, CAM_AUTO_GAIN);
  CAM_APPLY(set_gainceiling, static_cast<gainceiling_t>(CAM_GAIN_CEILING));
#undef CAM_APPLY

  Serial.printf("[CAMERA] Settings: vflip=%d hmirror=%d brightness=%d contrast=%d saturation=%d\n", CAM_VFLIP,
                CAM_HMIRROR, CAM_BRIGHTNESS, CAM_CONTRAST, CAM_SATURATION);
  Serial.printf("[CAMERA]           AWB=%d awb_gain=%d wb_mode=%d AEC=%d aec2=%d ae_level=%d AGC=%d gainceiling=%d\n",
                CAM_AUTO_WHITE_BALANCE, CAM_AWB_GAIN, CAM_WB_MODE, CAM_AUTO_EXPOSURE, CAM_AEC2, CAM_AE_LEVEL,
                CAM_AUTO_GAIN, CAM_GAIN_CEILING);
}

bool CameraModule::validate(const camera_fb_t* fb) {
  const pixformat_t expected = CAMERA_USE_JPEG ? PIXFORMAT_JPEG : PIXFORMAT_RGB565;
  if (fb->buf == nullptr || fb->len == 0) {
    error_ = CAM_ERR_NO_FRAME;
    Serial.println("[CAMERA] WARN: empty frame buffer");
    return false;
  }
  if (fb->format != expected) {
    error_ = CAM_ERR_BAD_FORMAT;
    Serial.printf("[CAMERA] WARN: unexpected pixel format %d (expected %d)\n", (int)fb->format, (int)expected);
    return false;
  }
  if (static_cast<int>(fb->width) != width_ || static_cast<int>(fb->height) != height_) {
    error_ = CAM_ERR_BAD_SIZE;
    Serial.printf("[CAMERA] WARN: unexpected size %ux%u (expected %dx%d)\n", (unsigned)fb->width,
                  (unsigned)fb->height, width_, height_);
    return false;
  }
#if CAMERA_USE_JPEG
  if (fb->len < 4 || fb->buf[0] != 0xFF || fb->buf[1] != 0xD8) {
    error_ = CAM_ERR_BAD_FORMAT;
    Serial.println("[CAMERA] WARN: JPEG frame without SOI marker");
    return false;
  }
#else
  const size_t need = static_cast<size_t>(width_) * height_ * 2;
  if (fb->len < need) {
    error_ = CAM_ERR_BAD_SIZE;
    Serial.printf("[CAMERA] WARN: RGB565 frame too short: %u < %u B\n", (unsigned)fb->len, (unsigned)need);
    return false;
  }
#endif
  return true;
}

bool CameraModule::capture(uint32_t* elapsedUs) {
  release();
  if (!ready_) {
    error_ = CAM_ERR_NOT_READY;
    return false;
  }
  const uint32_t t0 = micros();
  // Frame đang nằm sẵn trong buffer có thể được chụp TRƯỚC khi vật xuất hiện → bỏ đi.
  for (int i = 0; i < CAMERA_DISCARD_FRAMES; ++i) {
    camera_fb_t* old = esp_camera_fb_get();
    if (old != nullptr) esp_camera_fb_return(old);
  }
  for (int attempt = 1; attempt <= CAMERA_CAPTURE_RETRIES; ++attempt) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb == nullptr) {
      error_ = CAM_ERR_NO_FRAME;
      Serial.printf("[CAMERA] WARN: esp_camera_fb_get() returned NULL (attempt %d/%d)\n", attempt,
                    CAMERA_CAPTURE_RETRIES);
    } else if (!validate(fb)) {
      esp_camera_fb_return(fb);  // không dùng frame lỗi
      Serial.printf("[CAMERA] WARN: invalid frame (attempt %d/%d)\n", attempt, CAMERA_CAPTURE_RETRIES);
    } else {
      fb_ = fb;
      consecutiveFailures_ = 0;
      error_ = CAM_OK;
      if (elapsedUs) *elapsedUs = micros() - t0;
      return true;
    }
    if (attempt < CAMERA_CAPTURE_RETRIES) delay(CAMERA_RETRY_DELAY_MS);
  }
  ++consecutiveFailures_;
  if (error_ == CAM_OK) error_ = CAM_ERR_CAPTURE_FAILED;
  if (elapsedUs) *elapsedUs = micros() - t0;
  return false;
}

bool CameraModule::getImage(ImageView* out, uint32_t* decodeUs) {
  if (decodeUs) *decodeUs = 0;
  if (fb_ == nullptr || out == nullptr) {
    error_ = CAM_ERR_NO_FRAME;
    return false;
  }
#if CAMERA_USE_JPEG
  if (decodeBuf_ == nullptr) {
    error_ = CAM_ERR_NO_MEMORY;
    return false;
  }
  const uint32_t t0 = micros();
  if (!fmt2rgb888(fb_->buf, fb_->len, fb_->format, decodeBuf_)) {
    error_ = CAM_ERR_DECODE_FAILED;
    return false;
  }
  if (decodeUs) *decodeUs = micros() - t0;
  out->data = decodeBuf_;
  out->length = decodeBufSize_;
  out->layout = PIXEL_BGR888;  // fmt2rgb888() ghi theo thứ tự B, G, R
#else
  out->data = fb_->buf;
  out->length = fb_->len;
  out->layout = PIXEL_RGB565_BE;
#endif
  out->width = fb_->width;
  out->height = fb_->height;
  return true;
}

void CameraModule::release() {
  if (fb_ != nullptr) {
    esp_camera_fb_return(fb_);
    fb_ = nullptr;
  }
}

void CameraModule::end() {
  release();
  if (driverInstalled_) {
    esp_camera_deinit();
    driverInstalled_ = false;
  }
  ready_ = false;
}

bool CameraModule::reinit() {
  ++reinitCount_;
  Serial.printf("[CAMERA] Re-initializing camera (attempt %lu)...\n", (unsigned long)reinitCount_);
  end();
  delay(100);
  return begin();
}

const char* CameraModule::errorString(CameraError e) {
  switch (e) {
    case CAM_OK: return "OK";
    case CAM_ERR_NO_PSRAM: return "PSRAM not available";
    case CAM_ERR_INIT_FAILED: return "esp_camera_init failed";
    case CAM_ERR_SENSOR_NOT_FOUND: return "camera sensor not found";
    case CAM_ERR_WRONG_SENSOR: return "sensor is not OV3660";
    case CAM_ERR_NOT_READY: return "camera not initialized";
    case CAM_ERR_CAPTURE_FAILED: return "capture failed";
    case CAM_ERR_BAD_FORMAT: return "unexpected pixel format";
    case CAM_ERR_BAD_SIZE: return "unexpected frame size";
    case CAM_ERR_DECODE_FAILED: return "JPEG decode failed";
    case CAM_ERR_NO_MEMORY: return "out of memory";
    case CAM_ERR_NO_FRAME: return "no frame / empty frame";
  }
  return "unknown";
}
