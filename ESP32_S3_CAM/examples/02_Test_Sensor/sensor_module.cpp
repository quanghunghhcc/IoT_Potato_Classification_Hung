// sensor_module.cpp — xem sensor_module.h
#include "sensor_module.h"

#include "board_pins.h"
#include "config.h"

bool SensorModule::readActive() {
  rawLevel_ = digitalRead(SENSOR_PIN);
  return rawLevel_ == SENSOR_ACTIVE_LEVEL;
}

void SensorModule::begin() {
  pinMode(SENSOR_PIN, SENSOR_INPUT_MODE);
  delay(5);  // chờ mức logic ổn định sau khi cấu hình chân
  const uint32_t now = millis();
  rawActive_ = readActive();
  lastRaw_ = rawActive_;
  // Trạng thái ban đầu lấy theo mức đọc được; lúc khởi động không tính là một sự kiện "arrival".
  present_ = rawActive_;
  pending_ = false;
  lastChangeMs_ = now;
  lastArrivalMs_ = now;
  lastReleaseMs_ = now;
  Serial.printf("[SENSOR] GPIO%d, active level = %s, input mode = %s\n", SENSOR_PIN,
                SENSOR_ACTIVE_LEVEL == LOW ? "LOW" : "HIGH",
                SENSOR_INPUT_MODE == INPUT_PULLUP     ? "INPUT_PULLUP"
                : SENSOR_INPUT_MODE == INPUT_PULLDOWN ? "INPUT_PULLDOWN"
                                                       : "INPUT");
  Serial.printf("[SENSOR] Debounce %d ms, release debounce %d ms, cooldown %d ms\n", SENSOR_DEBOUNCE_MS,
                SENSOR_RELEASE_DEBOUNCE_MS, SENSOR_COOLDOWN_MS);
  Serial.printf("[SENSOR] Initial raw level = %d (%s)\n", rawLevel_, present_ ? "OBJECT" : "no object");
}

void SensorModule::update() {
  const uint32_t now = millis();
  const bool raw = readActive();
  rawActive_ = raw;

  if (raw != lastRaw_) {
    lastRaw_ = raw;
    lastChangeMs_ = now;
    if (raw == present_) {
      // Tín hiệu quay về trạng thái ổn định trước khi đủ thời gian debounce → xung nhiễu.
      if (pending_) ++glitches_;
      pending_ = false;
    } else {
      pending_ = true;
    }
  }

  if (pending_) {
    const uint32_t need = raw ? SENSOR_DEBOUNCE_MS : SENSOR_RELEASE_DEBOUNCE_MS;
    if (now - lastChangeMs_ >= need) {
      pending_ = false;
      present_ = raw;
      if (present_) {
        ++arrivals_;
        lastArrivalMs_ = now;
      } else {
        ++releases_;
        lastReleaseMs_ = now;
      }
    }
  }
}
