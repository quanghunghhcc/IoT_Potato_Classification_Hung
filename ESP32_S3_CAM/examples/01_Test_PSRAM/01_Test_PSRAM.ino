// 01_Test_PSRAM — Kiểm tra PSRAM của ESP32-S3-WROOM-1 N16R8 (độc lập, không cần module nào khác)
//
// Tools: Board = ESP32S3 Dev Module, Flash Size = 16MB, PSRAM = OPI PSRAM.
// Kỳ vọng: PSRAM found, tổng ~8 MB, cấp phát/ghi/đọc/giải phóng thành công, bộ nhớ trở về như cũ.
#include <Arduino.h>
#include <esp_heap_caps.h>

static bool testBlock(size_t size, bool aligned16) {
  const uint32_t before = ESP.getFreePsram();
  uint8_t* p = aligned16 ? static_cast<uint8_t*>(heap_caps_aligned_alloc(16, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
                         : static_cast<uint8_t*>(ps_malloc(size));
  if (p == nullptr) {
    Serial.printf("  [FAIL] alloc %u B%s\n", (unsigned)size, aligned16 ? " (16-byte aligned)" : "");
    return false;
  }
  const uint32_t tw = micros();
  for (size_t i = 0; i < size; ++i) p[i] = static_cast<uint8_t>((i * 31u + 7u) & 0xFFu);
  const uint32_t writeUs = micros() - tw;
  const uint32_t tr = micros();
  size_t errors = 0;
  for (size_t i = 0; i < size; ++i)
    if (p[i] != static_cast<uint8_t>((i * 31u + 7u) & 0xFFu)) ++errors;
  const uint32_t readUs = micros() - tr;
  const bool alignedOk = (reinterpret_cast<uintptr_t>(p) % 16) == 0;
  if (aligned16) heap_caps_free(p);
  else free(p);
  const uint32_t after = ESP.getFreePsram();
  Serial.printf("  %s alloc %7u B at %p%s: write %.1f MB/s, read+verify %.1f MB/s, errors=%u, free before/after = %lu/%lu\n",
                (errors == 0 && after == before && (!aligned16 || alignedOk)) ? "[PASS]" : "[FAIL]", (unsigned)size, p,
                aligned16 ? (alignedOk ? " (aligned 16)" : " (NOT aligned!)") : "", size / (writeUs + 1.0f),
                size / (readUs + 1.0f), (unsigned)errors, (unsigned long)before, (unsigned long)after);
  return errors == 0 && after == before && (!aligned16 || alignedOk);
}

void setup() {
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);
  delay(200);

  Serial.println();
  Serial.println("===== 01_Test_PSRAM =====");
  Serial.printf("Chip: %s rev %d, %d cores, %lu MHz\n", ESP.getChipModel(), (int)ESP.getChipRevision(),
                (int)ESP.getChipCores(), (unsigned long)ESP.getCpuFreqMHz());
  Serial.printf("Flash size (from Tools > Flash Size): %lu MB\n", (unsigned long)(ESP.getFlashChipSize() >> 20));
  Serial.printf("Internal heap: total %lu B, free %lu B, largest block %lu B\n", (unsigned long)ESP.getHeapSize(),
                (unsigned long)ESP.getFreeHeap(),
                (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

  if (!psramFound()) {
    Serial.println("[FAIL] PSRAM found: NO");
    Serial.println("       -> Tools > PSRAM > OPI PSRAM (module ESP32-S3-WROOM-1 N16R8 dung OPI PSRAM)");
    Serial.println("       -> Kiem tra lai ten module tren nap kim loai (N16R8 = 16MB flash, 8MB PSRAM)");
    return;
  }
  Serial.println("[PASS] PSRAM found: YES");
  Serial.printf("PSRAM Size: %lu B (%.2f MB)\n", (unsigned long)ESP.getPsramSize(), ESP.getPsramSize() / 1048576.0f);
  Serial.printf("Free PSRAM: %lu B, largest block %lu B\n", (unsigned long)ESP.getFreePsram(),
                (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
  Serial.printf("Free Heap: %lu B\n", (unsigned long)ESP.getFreeHeap());
  const bool sizeOk = ESP.getPsramSize() >= 7UL * 1024UL * 1024UL;
  Serial.printf("%s PSRAM size >= 7 MB (expected ~8 MB for N16R8)\n", sizeOk ? "[PASS]" : "[WARN]");

  Serial.println("Allocation tests:");
  bool ok = true;
  ok &= testBlock(256 * 1024, false);
  ok &= testBlock(320 * 1024, true);   // kích thước tensor arena mặc định
  ok &= testBlock(1024 * 1024, false);
  ok &= testBlock(4 * 1024 * 1024, false);

  Serial.printf("Free PSRAM after deallocation: %lu B\n", (unsigned long)ESP.getFreePsram());
  Serial.printf("RESULT: %s\n", ok && sizeOk ? "PASS" : "FAIL");
}

void loop() { delay(1000); }
