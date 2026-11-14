// Host stub of the Arduino-ESP32 API subset used by the firmware (syntax/functional checks only).
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdarg>
#include <chrono>
#include <thread>
#include "sdkconfig.h"
#include "esp_arduino_version.h"
#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define INPUT_PULLDOWN 0x09
#define ARDUINO_ISR_ATTR
inline uint32_t micros() { using namespace std::chrono; static auto t0 = steady_clock::now(); return (uint32_t)duration_cast<microseconds>(steady_clock::now() - t0).count(); }
inline uint32_t millis() { return micros() / 1000; }
inline void delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
inline void yield() {}
extern int g_stub_pin_level[64];
inline int digitalRead(uint8_t p) { return g_stub_pin_level[p]; }
inline void digitalWrite(uint8_t p, uint8_t v) { g_stub_pin_level[p] = v; }
inline void pinMode(uint8_t, uint8_t) {}
inline bool psramFound() { return true; }
inline void* ps_malloc(size_t n) { return std::malloc(n); }
class Print {
 public:
  virtual ~Print() {}
  size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) { va_list a; va_start(a, fmt); int n = std::vprintf(fmt, a); va_end(a); return n; }
  size_t println(const char* s = "") { return std::printf("%s\n", s); }
  size_t print(const char* s) { return std::printf("%s", s); }
};
class HardwareSerial : public Print {
 public:
  void begin(unsigned long) {}
  int available() { return 0; }
  int read() { return -1; }
  int peek() { return -1; }
  explicit operator bool() const { return true; }
};
extern HardwareSerial Serial;
class EspClass {
 public:
  const char* getChipModel() { return "ESP32-S3"; }
  uint8_t getChipRevision() { return 0; }
  uint8_t getChipCores() { return 2; }
  uint32_t getCpuFreqMHz() { return 240; }
  uint32_t getFlashChipSize() { return 16u << 20; }
  uint32_t getPsramSize() { return 8u << 20; }
  uint32_t getFreePsram() { return 8u << 20; }
  uint32_t getFreeHeap() { return 300000; }
  uint32_t getHeapSize() { return 400000; }
};
extern EspClass ESP;
inline const char* esp_get_idf_version() { return "v5.5-stub"; }
