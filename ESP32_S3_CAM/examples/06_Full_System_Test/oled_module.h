// oled_module.h — Điều khiển màn hình OLED 0.96 inch (SSD1306 128x64 I2C)
//
// Driver SSD1306 tích hợp sẵn qua TwoWire (<Wire.h>) chuẩn của ESP32:
//   * KHÔNG cần cài thêm bất kỳ thư viện ngoài nào (không cần Adafruit_SSD1306 hay GFX).
//   * Tự quản lý framebuffer 128x64 (1 KB RAM) và font chữ 5x7 chuẩn.
//   * Hiển thị trạng thái phân loại khoai tây: Boot, Waiting, Detecting, Capturing, Result, Error.
//
// Đấu nối phần cứng:
//   * VCC -> 3V3 của bo ESP32-S3
//   * GND -> GND
//   * SDA -> GPIO 1 (OLED_SDA_PIN trong board_pins.h)
//   * SCL -> GPIO 2 (OLED_SCL_PIN trong board_pins.h)
#pragma once

#include <Arduino.h>
#include <stdint.h>

class OledModule {
 public:
  bool begin(int sda = 1, int scl = 2, uint8_t addr = 0x3C);
  void clear();
  void display();

  // Các màn hình hiển thị trực quan theo chu trình
  void showBoot(const char* title, const char* version);
  void showReady(const char* ip, uint32_t total, uint32_t good, uint32_t notGood);
  void showDetecting(uint32_t seq);
  void showCapturing(uint32_t seq);
  void showInferring(uint32_t seq);
  void showResult(const char* quality, float confidence, uint32_t seq, float totalMs);
  void showError(const char* errorMsg);

  bool isReady() const { return ready_; }

  // Các hàm vẽ đồ họa cơ bản
  void drawPixel(int16_t x, int16_t y, uint8_t color);
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint8_t color);
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint8_t color);
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
  void drawChar(int16_t x, int16_t y, char c, uint8_t color, uint8_t bg, uint8_t size);
  void print(int16_t x, int16_t y, const char* text, uint8_t size = 1, uint8_t color = 1, uint8_t bg = 0);

 private:
  void writeCommand(uint8_t cmd);
  void writeData(const uint8_t* data, size_t size);

  uint8_t buffer_[1024] = {};
  uint8_t addr_ = 0x3C;
  bool ready_ = false;
};
