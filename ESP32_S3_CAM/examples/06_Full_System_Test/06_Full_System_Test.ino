// 06_Full_System_Test — Toàn hệ thống: Sensor → Camera → Preprocessing → CNN → LED, kèm báo cáo kiểm thử.
//
// Dùng ĐÚNG các module của firmware chính (sao chép bởi tools/sync_project.py). Khác biệt duy nhất:
// sau mỗi chu trình in một báo cáo [TEST] để đối chiếu với các test case trong TESTING.md:
//   * duplicate triggers  (TC04/TC12: phải luôn = 0 khi giữ vật trước cảm biến)
//   * glitches bị bỏ qua  (TC07: xung nhiễu ngắn không kích hoạt)
//   * lỗi chụp/inference  (TC08/TC09: không có kết quả GOOD/NOT_GOOD giả)
//   * chênh lệch bộ nhớ so với sau chu trình đầu tiên (TC11: không rò rỉ đáng kể)
// Lệnh Serial: như firmware chính (h, s, t, f, m, r) + 'x' = in báo cáo kiểm thử ngay.
#include <Arduino.h>

#include "board_pins.h"
#include "config.h"
#include "diagnostics.h"
#include "system_controller.h"

static SystemController gSystem;
static SystemState lastState = STATE_INIT;
static uint32_t lastReportedTriggers = 0;
static uint32_t lastPeriodicMs = 0;

static void printTestReport(const char* reason) {
  const SystemStats& s = gSystem.stats();
  const MemorySnapshot now = takeMemorySnapshot();
  Serial.println();
  Serial.printf("[TEST] ===== Report (%s) =====\n", reason);
  Serial.printf("[TEST] cycles started=%lu classified=%lu GOOD=%lu NOT_GOOD=%lu UNKNOWN=%lu\n",
                (unsigned long)s.triggers, (unsigned long)s.classifications, (unsigned long)s.good,
                (unsigned long)s.notGood, (unsigned long)s.unknown);
  Serial.printf("[TEST] duplicate triggers=%lu %s\n", (unsigned long)s.duplicateTriggers,
                s.duplicateTriggers == 0 ? "(OK)" : "(FAIL: object classified more than once)");
  Serial.printf("[TEST] re-detections in cooldown=%lu, cancelled before capture=%lu\n",
                (unsigned long)s.reDetectionsInCooldown, (unsigned long)s.abortedBeforeCapture);
  Serial.printf("[TEST] errors: capture=%lu preprocess=%lu inference=%lu\n", (unsigned long)s.captureErrors,
                (unsigned long)s.preprocessErrors, (unsigned long)s.inferenceErrors);
  if (s.classifications > 0) {
    Serial.printf("[TEST] inference ms: min %.1f avg %.1f max %.1f\n", s.inferenceUsMin / 1000.0f,
                  (double)s.inferenceUsSum / s.classifications / 1000.0, s.inferenceUsMax / 1000.0f);
    Serial.printf("[TEST] internal RAM free: first cycle %lu, now %lu (delta %ld B); min ever %lu\n",
                  (unsigned long)s.memAfterFirstCycle.internalFree, (unsigned long)now.internalFree,
                  (long)now.internalFree - (long)s.memAfterFirstCycle.internalFree,
                  (unsigned long)now.internalMinFree);
    Serial.printf("[TEST] PSRAM free:        first cycle %lu, now %lu (delta %ld B)\n",
                  (unsigned long)s.memAfterFirstCycle.psramFree, (unsigned long)now.psramFree,
                  (long)now.psramFree - (long)s.memAfterFirstCycle.psramFree);
  }
  Serial.printf("[TEST] state=%s uptime=%lu s\n", SystemController::stateName(gSystem.state()),
                (unsigned long)(millis() / 1000));
  Serial.println("[TEST] ==============================");
  Serial.println();
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < SERIAL_WAIT_MS) delay(10);
  Serial.println();
  Serial.println("===== 06_Full_System_Test =====");
  Serial.println("Thu: dua khoai tay vao, GIU YEN > 10 s (khong duoc phan loai lai), lay ra, dua cu moi vao.");
  if (!gSystem.begin()) Serial.println("[TEST] Initialization FAILED -> see [ERROR] lines");
}

void loop() {
  gSystem.update();

  // 'x' = báo cáo ngay; các lệnh khác chuyển cho controller.
  if (Serial.peek() == 'x') {
    Serial.read();
    printTestReport("on request");
  } else {
    gSystem.pollSerialCommands();
  }

  // Báo cáo sau mỗi chu trình: khi quay về IDLE và số chu trình đã tăng.
  const SystemState st = gSystem.state();
  if (st == STATE_IDLE && lastState != STATE_IDLE && gSystem.stats().triggers != lastReportedTriggers) {
    lastReportedTriggers = gSystem.stats().triggers;
    printTestReport("cycle finished");
  }
  lastState = st;

  if (millis() - lastPeriodicMs >= 60000) {
    lastPeriodicMs = millis();
    printTestReport("periodic 60 s");
  }
  delay(1);
}
