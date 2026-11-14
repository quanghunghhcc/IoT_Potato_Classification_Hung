// 04_Test_LEDs — Kiểm tra hai LED kết quả (không cần camera/CNN).
//
// Trình tự lặp lại: Green ON → Green OFF → Red ON → Red OFF → All OFF → Error indication (nháy cả hai)
// → All OFF. Nếu LED sáng ngược (sáng khi phải tắt) → đổi LED_ACTIVE_LEVEL trong config.h.
#include <Arduino.h>

#include "board_pins.h"
#include "config.h"
#include "led_control.h"

static LedControl leds;

static void waitMs(uint32_t ms) {
  const uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    leds.update();  // cần cho chế độ nháy báo lỗi
    delay(5);
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < SERIAL_WAIT_MS) delay(10);
  delay(200);
  Serial.println();
  Serial.println("===== 04_Test_LEDs =====");
  leds.begin();  // in: [LED] ... both OFF
}

void loop() {
  static uint32_t roundNo = 0;
  Serial.printf("--- Round %lu ---\n", (unsigned long)++roundNo);
  leds.showGood();     // [LED] GREEN ON  (đỏ phải TẮT)
  waitMs(1500);
  leds.allOff();       // [LED] GREEN OFF
  waitMs(500);
  leds.showNotGood();  // [LED] RED ON    (xanh phải TẮT)
  waitMs(1500);
  leds.allOff();       // [LED] RED OFF
  waitMs(500);
  Serial.println("[LED] All OFF");
  waitMs(1000);
  leds.showError();    // cả hai nháy CÙNG LÚC = lỗi hệ thống (khác NOT_GOOD)
  waitMs(3000);
  leds.allOff();
  waitMs(1500);
}
