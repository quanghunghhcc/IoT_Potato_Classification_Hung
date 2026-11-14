// sensor_module.h — Cảm biến vật cản Obstacle Avoidance V1 (đọc GPIO, debounce không chặn)
//
// Mô hình hoạt động:
//   * update() được gọi liên tục trong loop(); không dùng delay().
//   * "present" chỉ chuyển sang true khi mức active ổn định >= SENSOR_DEBOUNCE_MS.
//   * "present" chỉ chuyển về false khi mức inactive ổn định >= SENSOR_RELEASE_DEBOUNCE_MS.
//   * Xung ngắn hơn thời gian debounce được đếm là glitch và bị bỏ qua.
#pragma once

#include <Arduino.h>

class SensorModule {
 public:
  void begin();
  void update();

  bool isPresent() const { return present_; }   // trạng thái đã debounce
  bool rawActive() const { return rawActive_; }  // mức tức thời (đã quy đổi theo cực tính)
  int rawLevel() const { return rawLevel_; }      // HIGH/LOW đọc trực tiếp từ GPIO

  uint32_t arrivalCount() const { return arrivals_; }
  uint32_t releaseCount() const { return releases_; }
  uint32_t glitchCount() const { return glitches_; }

  uint32_t lastArrivalMs() const { return lastArrivalMs_; }
  uint32_t lastReleaseMs() const { return lastReleaseMs_; }
  // Thời gian đã ở trạng thái hiện tại (đã debounce) tính đến thời điểm now.
  uint32_t presentForMs(uint32_t now) const { return present_ ? now - lastArrivalMs_ : 0; }
  uint32_t absentForMs(uint32_t now) const { return present_ ? 0 : now - lastReleaseMs_; }

 private:
  bool readActive();

  bool present_ = false;
  bool rawActive_ = false;
  int rawLevel_ = HIGH;
  bool lastRaw_ = false;
  bool pending_ = false;  // đang có thay đổi chờ debounce
  uint32_t lastChangeMs_ = 0;
  uint32_t arrivals_ = 0;
  uint32_t releases_ = 0;
  uint32_t glitches_ = 0;
  uint32_t lastArrivalMs_ = 0;
  uint32_t lastReleaseMs_ = 0;
};
