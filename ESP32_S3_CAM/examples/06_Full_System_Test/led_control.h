// led_control.h — Hai LED kết quả (xanh = GOOD, đỏ = NOT_GOOD) + chỉ báo lỗi hệ thống riêng biệt.
//
// Quy tắc:
//   * GOOD     : xanh sáng liên tục, đỏ tắt.
//   * NOT_GOOD : đỏ sáng liên tục, xanh tắt.
//   * Lỗi hệ thống: hai LED nhấp nháy CÙNG LÚC (nếu LED_ERROR_INDICATION = 1) — không bao giờ dùng
//     "đỏ sáng liên tục" để báo lỗi, nên không thể nhầm với NOT_GOOD.
//   * Không bao giờ bật cả hai LED sáng liên tục trong chế độ phân loại bình thường.
#pragma once

#include <Arduino.h>

enum LedMode : uint8_t { LED_MODE_OFF = 0, LED_MODE_GOOD, LED_MODE_NOT_GOOD, LED_MODE_ERROR };

class LedControl {
 public:
  void begin();
  void allOff();
  void showGood();
  void showNotGood();
  void showError();      // bắt đầu nhấp nháy báo lỗi; cần gọi update() thường xuyên
  void update();         // điều khiển nhấp nháy (không chặn)
  LedMode mode() const { return mode_; }
  void setVerbose(bool v) { verbose_ = v; }

 private:
  void write(int pin, bool on);
  LedMode mode_ = LED_MODE_OFF;
  bool blinkOn_ = false;
  uint32_t lastToggleMs_ = 0;
  bool verbose_ = true;
};
