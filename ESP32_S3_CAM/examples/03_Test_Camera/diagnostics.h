// diagnostics.h — Tiện ích chẩn đoán: CRC32, xuất dữ liệu nhị phân qua Serial (base64), in bộ nhớ.
#pragma once

#include <Arduino.h>

// CRC-32 chuẩn (đa thức 0xEDB88320, giống zlib.crc32 / binascii.crc32 trong Python).
// Gọi lần đầu với crc = 0.
uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t length);

// Xuất một khối dữ liệu nhị phân dạng:
//   BEGIN_<kind> <header>
//   <base64, 76 ký tự/dòng>
//   END_<kind> len=<n> crc32=<hex>
// tools/serial_compare.py đọc định dạng này.
void dumpBase64Block(Print& out, const char* kind, const char* header, const uint8_t* data, size_t length);

// In thông tin bộ nhớ: RAM nội (internal), PSRAM, khối lớn nhất, mức thấp nhất từng đạt.
void printMemoryInfo(const char* tag);

struct MemorySnapshot {
  uint32_t internalFree;
  uint32_t internalMinFree;
  uint32_t internalLargest;
  uint32_t psramFree;
  uint32_t psramMinFree;
  uint32_t psramLargest;
};
MemorySnapshot takeMemorySnapshot();
