// Host stub of the Arduino-ESP32 WiFi API subset used by network_reporter.cpp (syntax check only).
#pragma once
#include <Arduino.h>
#include <string>
typedef enum { WL_IDLE_STATUS = 0, WL_CONNECTED = 3, WL_DISCONNECTED = 6 } wl_status_t;
typedef enum { WIFI_STA = 1 } wifi_mode_t;
class String { public: String(const char* s = "") : s_(s) {} const char* c_str() const { return s_.c_str(); } private: std::string s_; };
class IPAddress { public: String toString() const { return String("0.0.0.0"); } };
class WiFiClass { public:
  bool mode(wifi_mode_t) { return true; } bool setHostname(const char*) { return true; } bool setAutoReconnect(bool) { return true; }
  wl_status_t begin(const char*, const char*) { return WL_DISCONNECTED; } bool disconnect() { return true; }
  wl_status_t status() { return WL_DISCONNECTED; } IPAddress localIP() { return IPAddress(); } int8_t RSSI() { return 0; } };
extern WiFiClass WiFi;
inline void configTime(long, int, const char*, const char* = nullptr, const char* = nullptr) {}
#include <ctime>
#ifdef _WIN32  // newlib on the ESP32 has POSIX gmtime_r; MinGW does not
inline struct tm* gmtime_r(const time_t* t, struct tm* out) { return gmtime_s(out, t) == 0 ? out : nullptr; }
#endif
