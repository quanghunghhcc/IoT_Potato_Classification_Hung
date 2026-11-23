// 07_Test_OLED — Kiểm tra màn hình OLED 0.96 inch SSD1306 / SH1106 I2C độc lập (không cần camera/CNN).
//
// Màn hình sẽ hiển thị:
// 1. Màn hình khởi động Boot splash
// 2. Đồng hồ đếm giây + thông số + thanh đồ họa tiến trình chạy liên tục.
//
// Nối dây:
//   VCC -> 3.3V (hoặc 5V tùy chân module)
//   GND -> GND
//   SDA -> GPIO 1 (OLED_SDA_PIN)
//   SCL -> GPIO 2 (OLED_SCL_PIN)
#include <Arduino.h>

#include "board_pins.h"
#include "config.h"
#include "oled_module.h"

static OledModule oled;

void setup() {
  Serial.begin(SERIAL_BAUD);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < SERIAL_WAIT_MS) delay(10);
  delay(200);

  Serial.println();
  Serial.println("==========================================");
  Serial.println("             07_Test_OLED                 ");
  Serial.println("==========================================");
  Serial.printf("[TEST] Testing OLED on SDA=GPIO%d, SCL=GPIO%d\n", OLED_SDA_PIN, OLED_SCL_PIN);

  if (oled.begin(OLED_SDA_PIN, OLED_SCL_PIN, OLED_I2C_ADDR)) {
    Serial.println("[TEST] OLED initialized successfully!");
    oled.showBoot("POTATO TEST", "v1.0");
    delay(2000);
  } else {
    Serial.println("[TEST] OLED initialization FAILED! Check wiring and Serial logs above.");
  }
}

void loop() {
  static uint32_t count = 0;
  if (!oled.isReady()) {
    delay(1000);
    return;
  }
  ++count;
  oled.clear();
  oled.fillRect(0, 0, 128, 12, 1);
  oled.print(12, 2, "OLED TEST OK!", 1, 0, 1);

  char buf[32];
  snprintf(buf, sizeof(buf), "Count: %lu", (unsigned long)count);
  oled.print(12, 20, buf, 1, 1, 0);

  snprintf(buf, sizeof(buf), "Uptime: %lu s", (unsigned long)(millis() / 1000));
  oled.print(12, 34, buf, 1, 1, 0);

  // Thanh đồ họa tiến trình
  const int progress = (count * 4) % 108;
  oled.drawRect(10, 48, 108, 10, 1);
  oled.fillRect(10, 48, progress, 10, 1);

  oled.display();
  Serial.printf("[OLED] Display running: count=%lu, uptime=%lu s\n", (unsigned long)count, (unsigned long)(millis() / 1000));
  delay(500);
}
