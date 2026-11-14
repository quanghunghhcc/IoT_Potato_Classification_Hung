// =============================================================================================
//  ESP32-S3 AI Potato Quality Classification System
//
//  Obstacle Avoidance V1 phát hiện khoai tây → OV3660 chụp ảnh (QVGA RGB565) → tiền xử lý
//  RGB 96x96 INT8 → CNN INT8 (TensorFlow Lite Micro, chạy hoàn toàn trên ESP32-S3) →
//  GOOD = LED xanh / NOT_GOOD = LED đỏ → chờ khoai tây rời đi → sẵn sàng cho củ tiếp theo.
//
//  Môi trường: Arduino IDE 2.x + "esp32 by Espressif Systems" (Arduino-ESP32 Core 3.x).
//  Không cần cài thêm thư viện TensorFlow: core 3.x đã tích hợp esp-tflite-micro.
//  Cấu hình Tools (xem README.md): Board = ESP32S3 Dev Module, Flash Size = 16MB,
//  PSRAM = OPI PSRAM, Partition = 16M Flash (3MB APP/9.9MB FATFS), CPU = 240MHz.
//
//  Tham số: config.h        Chân GPIO: board_pins.h (PHẢI kiểm tra với bo thật)
// =============================================================================================
#include <Arduino.h>

#include "board_pins.h"
#include "camera_module.h"
#include "cnn_inference.h"
#include "config.h"
#include "led_control.h"
#include "sensor_module.h"
#include "system_controller.h"

// Toàn bộ trạng thái hệ thống nằm trong một đối tượng điều phối (state machine).
static SystemController gSystem;

void setup() {
  Serial.begin(SERIAL_BAUD);
  // Với USB CDC, chờ Serial Monitor tối đa SERIAL_WAIT_MS để không mất log khởi động
  // (không chặn vô hạn khi chạy độc lập không có máy tính).
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < SERIAL_WAIT_MS) delay(10);

  // STATE_INIT: kiểm tra PSRAM → LED (tắt) → cảm biến → camera OV3660 → TFLM (model, arena, tensor).
  // Nếu lỗi, hệ thống vào STATE_ERROR: báo lỗi bằng log + hai LED nháy cùng lúc, KHÔNG phân loại.
  if (!gSystem.begin()) {
    Serial.println("[BOOT] Initialization incomplete -> see [ERROR] lines above");
  }
}

void loop() {
  // Một bước state machine: đọc cảm biến (debounce), chụp/phân loại khi có sự kiện hợp lệ,
  // điều khiển LED, xử lý lỗi/timeout. Không dùng delay() dài.
  gSystem.update();

  // Lệnh chẩn đoán qua Serial Monitor (gõ 'h' để xem danh sách).
  gSystem.pollSerialCommands();

  // Nhường CPU cho các task nền (watchdog idle task), chu kỳ đọc cảm biến ~1 ms.
  delay(1);
}
