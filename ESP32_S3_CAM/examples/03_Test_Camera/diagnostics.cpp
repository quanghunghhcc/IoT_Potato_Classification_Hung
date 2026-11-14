// diagnostics.cpp — xem diagnostics.h
#include "diagnostics.h"

#include <esp_heap_caps.h>

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t length) {
  // Bảng 16 phần tử (xử lý từng nibble): nhỏ gọn, đủ nhanh cho ảnh 150 KB.
  static const uint32_t kTable[16] = {
      0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u,
      0x4DB26158u, 0x5005713Cu, 0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
      0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
  crc = ~crc;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    crc = (crc >> 4) ^ kTable[crc & 0x0Fu];
    crc = (crc >> 4) ^ kTable[crc & 0x0Fu];
  }
  return ~crc;
}

void dumpBase64Block(Print& out, const char* kind, const char* header, const uint8_t* data, size_t length) {
  static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  out.printf("BEGIN_%s %s\n", kind, header ? header : "");
  char line[80];
  size_t pos = 0;
  for (size_t i = 0; i < length; i += 3) {
    const uint32_t b0 = data[i];
    const uint32_t b1 = (i + 1 < length) ? data[i + 1] : 0;
    const uint32_t b2 = (i + 2 < length) ? data[i + 2] : 0;
    const uint32_t v = (b0 << 16) | (b1 << 8) | b2;
    line[pos++] = kAlphabet[(v >> 18) & 0x3F];
    line[pos++] = kAlphabet[(v >> 12) & 0x3F];
    line[pos++] = (i + 1 < length) ? kAlphabet[(v >> 6) & 0x3F] : '=';
    line[pos++] = (i + 2 < length) ? kAlphabet[v & 0x3F] : '=';
    if (pos >= 76) {
      line[pos] = '\0';
      out.println(line);
      pos = 0;
      yield();  // dump dài: nhường CPU, tránh watchdog
    }
  }
  if (pos > 0) {
    line[pos] = '\0';
    out.println(line);
  }
  out.printf("END_%s len=%lu crc32=%08lx\n", kind, (unsigned long)length,
             (unsigned long)crc32Update(0, data, length));
}

MemorySnapshot takeMemorySnapshot() {
  MemorySnapshot s;
  s.internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  s.internalMinFree = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  s.internalLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  s.psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  s.psramMinFree = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
  s.psramLargest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  return s;
}

void printMemoryInfo(const char* tag) {
  const MemorySnapshot s = takeMemorySnapshot();
  Serial.printf("[%s] Internal RAM free: %lu B (min %lu, largest block %lu)\n", tag,
                (unsigned long)s.internalFree, (unsigned long)s.internalMinFree,
                (unsigned long)s.internalLargest);
  Serial.printf("[%s] PSRAM free: %lu B (min %lu, largest block %lu)\n", tag, (unsigned long)s.psramFree,
                (unsigned long)s.psramMinFree, (unsigned long)s.psramLargest);
}
