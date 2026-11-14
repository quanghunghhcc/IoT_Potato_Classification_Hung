// 02_Test_Sensor — Kiểm tra cảm biến Obstacle Avoidance V1 + logic debounce dùng trong firmware chính.
//
// Dùng chung config.h / board_pins.h / sensor_module.* với firmware (sao chép bởi tools/sync_project.py).
// Cách kiểm tra cực tính:
//   * Không có vật: "Sensor GPIO" phải ở mức KHÁC SENSOR_ACTIVE_LEVEL.
//   * Đưa tay/khoai tây vào: mức đổi thành SENSOR_ACTIVE_LEVEL, LED trên module sáng, in "Object detected".
//   * Nếu ngược lại (báo có vật khi không có gì) → đổi SENSOR_ACTIVE_LEVEL trong config.h.
// Đo điện áp chân OUT trước khi nối: phải <= 3.3 V (xem WIRING.md).
#include <Arduino.h>

#include "board_pins.h"
#include "config.h"
#include "sensor_module.h"

static SensorModule sensor;
static uint32_t lastArrivals = 0;
static uint32_t lastReleases = 0;
static uint32_t lastGlitches = 0;
static uint32_t lastPrintMs = 0;
static uint32_t rawEdges = 0;
static int lastRawLevel = -1;

void setup() {
  Serial.begin(SERIAL_BAUD);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < SERIAL_WAIT_MS) delay(10);
  delay(200);
  Serial.println();
  Serial.println("===== 02_Test_Sensor =====");
  sensor.begin();
  Serial.printf("Object present when GPIO%d == %s. Raw level printed every 200 ms.\n", SENSOR_PIN,
                SENSOR_ACTIVE_LEVEL == LOW ? "0 (LOW)" : "1 (HIGH)");
  Serial.println("Events below are DEBOUNCED exactly like the main firmware.");
  lastRawLevel = sensor.rawLevel();
}

void loop() {
  sensor.update();
  const uint32_t now = millis();

  if (sensor.rawLevel() != lastRawLevel) {
    lastRawLevel = sensor.rawLevel();
    ++rawEdges;
  }
  if (now - lastPrintMs >= 200) {
    lastPrintMs = now;
    Serial.printf("Sensor GPIO: %d\n", sensor.rawLevel());
  }
  if (sensor.arrivalCount() != lastArrivals) {
    lastArrivals = sensor.arrivalCount();
    Serial.printf("Object detected   (arrival #%lu, stable >= %d ms)\n", (unsigned long)lastArrivals,
                  SENSOR_DEBOUNCE_MS);
  }
  if (sensor.releaseCount() != lastReleases) {
    lastReleases = sensor.releaseCount();
    Serial.printf("Object removed    (release #%lu, stable >= %d ms)\n", (unsigned long)lastReleases,
                  SENSOR_RELEASE_DEBOUNCE_MS);
  }
  if (sensor.glitchCount() != lastGlitches) {
    lastGlitches = sensor.glitchCount();
    Serial.printf("Short pulse ignored (glitch #%lu) -> debounce works\n", (unsigned long)lastGlitches);
  }
  if (Serial.available() > 0 && Serial.read() == 's') {
    Serial.printf("[STATS] raw edges=%lu arrivals=%lu releases=%lu glitches=%lu present=%d\n",
                  (unsigned long)rawEdges, (unsigned long)sensor.arrivalCount(),
                  (unsigned long)sensor.releaseCount(), (unsigned long)sensor.glitchCount(), sensor.isPresent());
  }
  delay(1);
}
