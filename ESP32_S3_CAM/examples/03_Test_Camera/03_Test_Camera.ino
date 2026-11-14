// 03_Test_Camera — Kiểm tra camera OV3660 bằng đúng camera_module của firmware chính.
//
// Khi khởi động: init camera, in PID cảm biến, chụp 5 frame, in độ phân giải / định dạng / dung lượng /
// thời gian chụp, trả frame buffer. Lệnh qua Serial Monitor (115200, "No line ending" hoặc bất kỳ):
//   c : chụp 1 frame và in thông tin
//   b : benchmark 20 lần chụp
//   d : chụp và xuất frame gốc (BEGIN_FRAME ... END_FRAME, base64) → tools/serial_compare.py dựng PNG
//       để kiểm tra MÀU, HƯỚNG (lật/xoay) và VÙNG NHÌN của ảnh
//   p : chụp + tiền xử lý LETTERBOX 96x96 (scale/zero_point lấy theo potato_metadata.json) và xuất tensor
// Chức năng xuất ảnh chỉ phục vụ kiểm thử, firmware chính không phụ thuộc vào nó.
#include <Arduino.h>

#include "board_pins.h"
#include "camera_module.h"
#include "config.h"
#include "diagnostics.h"
#include "image_preprocessing.h"

static CameraModule camera;
static int8_t tensor[96 * 96 * 3];
static uint32_t frameNo = 0;

static bool captureAndPrint(bool quiet) {
  uint32_t us = 0;
  if (!camera.capture(&us)) {
    Serial.printf("[CAMERA] Capture FAILED: %s\n", CameraModule::errorString(camera.lastError()));
    return false;
  }
  const camera_fb_t* fb = camera.frame();
  ++frameNo;
  if (!quiet) {
    Serial.printf("[CAMERA] Frame #%lu: %ux%u, format=%s, len=%u B, capture=%.1f ms\n", (unsigned long)frameNo,
                  (unsigned)fb->width, (unsigned)fb->height, fb->format == PIXFORMAT_JPEG ? "JPEG" : "RGB565",
                  (unsigned)fb->len, us / 1000.0f);
  }
  return true;
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < SERIAL_WAIT_MS) delay(10);
  delay(200);
  Serial.println();
  Serial.println("===== 03_Test_Camera =====");
  if (!psramFound()) {
    Serial.println("[FAIL] PSRAM not found -> Tools > PSRAM > OPI PSRAM");
    return;
  }
  if (!camera.begin()) {
    Serial.printf("[FAIL] Camera init: %s\n", CameraModule::errorString(camera.lastError()));
    return;
  }
  Serial.printf("[PASS] Sensor %s, PID 0x%04X %s\n", camera.sensorName(), (unsigned)camera.sensorPid(),
                camera.sensorPid() == 0x3660 ? "(OV3660)" : "(NOT OV3660)");
  for (int i = 0; i < 5; ++i) {
    if (captureAndPrint(false)) camera.release();  // trả frame buffer sau mỗi lần dùng
  }
  printMemoryInfo("MEMORY");
  Serial.println("Commands: c=capture  b=benchmark  d=dump frame  p=dump preprocessed 96x96 tensor");
}

void loop() {
  if (!camera.isReady() || Serial.available() <= 0) {
    delay(10);
    return;
  }
  const int cmd = Serial.read();
  if (cmd == 'c') {
    if (captureAndPrint(false)) camera.release();
  } else if (cmd == 'b') {
    uint32_t okCount = 0;
    const uint32_t t0 = millis();
    for (int i = 0; i < 20; ++i) {
      if (captureAndPrint(true)) {
        ++okCount;
        camera.release();
      }
    }
    Serial.printf("[CAMERA] Benchmark: %lu/20 OK, avg %.1f ms per capture (incl. %d discarded frame)\n",
                  (unsigned long)okCount, (millis() - t0) / 20.0f, CAMERA_DISCARD_FRAMES);
    printMemoryInfo("MEMORY");
  } else if (cmd == 'd') {
    if (captureAndPrint(false)) {
      const camera_fb_t* fb = camera.frame();
      char header[96];
      snprintf(header, sizeof(header), "seq=%lu w=%u h=%u format=%s", (unsigned long)frameNo, (unsigned)fb->width,
               (unsigned)fb->height, fb->format == PIXFORMAT_JPEG ? "JPEG" : "RGB565_BE");
      dumpBase64Block(Serial, "FRAME", header, fb->buf, fb->len);
      camera.release();
    }
  } else if (cmd == 'p') {
    if (captureAndPrint(false)) {
      ImageView img;
      uint32_t decodeUs = 0;
      PreprocStats st = {};
      // Giá trị lượng tử hóa theo potato_metadata.json (firmware chính đọc trực tiếp từ tensor).
      const float scale = 1.0f;
      const int32_t zeroPoint = -128;
      PreprocError e = PREPROC_ERR_NULL_POINTER;
      if (camera.getImage(&img, &decodeUs)) {
        e = preprocessToInt8(img, PREPROC_FRAMING, tensor, sizeof(tensor), 96, 96, scale, zeroPoint, &st);
      }
      camera.release();
      if (e != PREPROC_OK) {
        Serial.printf("[IMAGE] preprocessing failed: %s\n", preprocErrorString(e));
        return;
      }
      Serial.printf("[IMAGE] %s, pad RGB(%u,%u,%u), %.1f ms, CRC32 %08lx\n", framingName(st.framing),
                    st.padColor[0], st.padColor[1], st.padColor[2], (decodeUs + st.elapsedUs) / 1000.0f,
                    (unsigned long)st.tensorCrc32);
      char header[128];
      snprintf(header, sizeof(header), "seq=%lu shape=1x96x96x3 dtype=int8 scale=%.8f zero_point=%ld framing=%s",
               (unsigned long)frameNo, scale, (long)zeroPoint, framingName(st.framing));
      dumpBase64Block(Serial, "TENSOR", header, reinterpret_cast<const uint8_t*>(tensor), sizeof(tensor));
    }
  }
}
