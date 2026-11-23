// system_controller.cpp — xem system_controller.h
#include "system_controller.h"

#include <esp_arduino_version.h>
#include <esp_system.h>

#include "board_pins.h"
#include "config.h"
#include "model_data.h"

// ---- Kiểm tra cấu hình biên dịch --------------------------------------------------------------
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Firmware nay chi danh cho ESP32-S3: Tools > Board > ESP32S3 Dev Module"
#endif
#if !defined(ESP_ARDUINO_VERSION_MAJOR) || ESP_ARDUINO_VERSION_MAJOR < 3
#error "Can Arduino-ESP32 core >= 3.0.0 (TensorFlow Lite Micro tich hop san trong core)"
#endif
#ifndef BOARD_HAS_PSRAM
#warning "PSRAM chua duoc bat: Tools > PSRAM > OPI PSRAM (firmware se bao loi khi chay)"
#endif

namespace {

const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external pin";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "PANIC (exception)";
    case ESP_RST_INT_WDT: return "INTERRUPT WATCHDOG";
    case ESP_RST_TASK_WDT: return "TASK WATCHDOG";
    case ESP_RST_WDT: return "OTHER WATCHDOG";
    case ESP_RST_DEEPSLEEP: return "deep-sleep wake";
    case ESP_RST_BROWNOUT: return "BROWNOUT (nguon yeu)";
    case ESP_RST_SDIO: return "SDIO";
    default: return "other";
  }
}

inline float msf(uint32_t us) { return us / 1000.0f; }

}  // namespace

// =============================================================================================
// Khởi tạo
// =============================================================================================
void SystemController::printBanner() {
  Serial.println();
  Serial.println("====================================");
  Serial.println(" ESP32-S3 AI POTATO CLASSIFIER");
  Serial.println("====================================");
  Serial.println();
  Serial.println("[BOOT] Starting system...");
  Serial.printf("[BOOT] Firmware: %s v%s (build %s %s)\n", FW_NAME, FW_VERSION, __DATE__, __TIME__);
  Serial.printf("[BOOT] Chip: %s rev %d, %d cores @ %lu MHz\n", ESP.getChipModel(), (int)ESP.getChipRevision(),
                (int)ESP.getChipCores(), (unsigned long)ESP.getCpuFreqMHz());
  Serial.printf("[BOOT] Arduino-ESP32 core %d.%d.%d, ESP-IDF %s\n", ESP_ARDUINO_VERSION_MAJOR,
                ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH, esp_get_idf_version());
  Serial.printf("[BOOT] Flash detected: %lu MB\n", (unsigned long)(ESP.getFlashChipSize() / (1024UL * 1024UL)));
  Serial.printf("[BOOT] Reset reason: %s\n", resetReasonName(esp_reset_reason()));
  Serial.printf("[BOOT] Board pin profile: %s%s\n", BOARD_PROFILE_NAME,
                BOARD_PINOUT_VERIFIED ? " (verified)" : " (CHUA XAC MINH - xem board_pins.h / WIRING.md)");
}

bool SystemController::begin() {
  state_ = STATE_INIT;
  stateSinceMs_ = millis();
  resetResult(result_);
  printBanner();

  // LED trước tiên: đảm bảo cả hai LED TẮT ngay khi khởi động.
  leds_.begin();

  // PSRAM: bắt buộc cho frame buffer camera và tensor arena.
  if (!psramFound()) {
    Serial.println("[BOOT] ERROR: PSRAM not found -> Tools > PSRAM > OPI PSRAM (module WROOM-1 N16R8)");
    sensor_.begin();
    enterError(SYS_ERR_PSRAM, true, false);
    return false;
  }
  Serial.printf("[BOOT] PSRAM detected: %lu B total, %lu B free\n", (unsigned long)ESP.getPsramSize(),
                (unsigned long)ESP.getFreePsram());
  Serial.printf("[BOOT] Free Heap (internal): %lu B\n", (unsigned long)ESP.getFreeHeap());
  if (ESP.getPsramSize() < 7UL * 1024UL * 1024UL) {
    Serial.println("[BOOT] WARNING: PSRAM < 8 MB; module N16R8 phai co 8 MB OPI PSRAM - kiem tra Tools > PSRAM");
  }
  Serial.println();

  Serial.println("[SENSOR] Initializing...");
  sensor_.begin();
  Serial.println("[SENSOR] Sensor ready");
  Serial.println();

  if (!camera_.begin()) {
    enterError(SYS_ERR_CAMERA_INIT, true, true);
    return false;
  }
  Serial.println();

  if (!cnn::begin()) {
    // Lỗi model là xác định (dữ liệu/op/arena) → thử lại không giúp gì, trừ khi thiếu bộ nhớ.
    enterError(SYS_ERR_MODEL_INIT, true, cnn::lastError() == CNN_ERR_ARENA_ALLOC);
    return false;
  }
  Serial.println();
  // Mạng khởi động SAU camera + CNN để chức năng chính được ưu tiên bộ nhớ; lỗi mạng không chặn hệ thống.
  net_.begin();
  Serial.println();
  printMemoryInfo("MEMORY");
  Serial.println();
  goReady();
  return true;
}

void SystemController::goReady() {
  leds_.allOff();
  sensor_.update();
  // Lúc khởi động: theo SENSOR_REQUIRE_CLEAR_AT_BOOT. Sau khi khôi phục lỗi: luôn chờ vật hiện tại
  // rời đi (không thử lại trên cùng một vật).
  const bool requireClear = firstReady_ ? (SENSOR_REQUIRE_CLEAR_AT_BOOT != 0) : true;
  firstReady_ = false;
  if (requireClear && sensor_.isPresent()) {
    Serial.println("[SENSOR] Object/active signal present -> waiting for removal before the next classification");
    arrivalsAtLastTrigger_ = sensor_.arrivalCount();
    enterState(STATE_WAIT_REMOVAL);
    return;
  }
  Serial.println("[SYSTEM] READY");
  Serial.println("[SYSTEM] Waiting for potato...");
  enterState(STATE_IDLE);
}

// =============================================================================================
// Vòng lặp chính
// =============================================================================================
void SystemController::enterState(SystemState s) {
  state_ = s;
  stateSinceMs_ = millis();
  stateEntry_ = true;
}

void SystemController::update() {
  sensor_.update();
  leds_.update();
  net_.loop();  // không chặn
  // Lấy thời gian SAU sensor_.update() để now >= mọi mốc thời gian cảm biến vừa ghi (tránh tràn số
  // khi tính now - lastReleaseMs).
  const uint32_t now = millis();
  switch (state_) {
    case STATE_INIT: break;  // begin() đã chuyển trạng thái
    case STATE_IDLE: handleIdle(now); break;
    case STATE_OBJECT_DETECTED: handleObjectDetected(now); break;
    case STATE_CAMERA_CAPTURE: handleCapture(); break;
    case STATE_PREPROCESSING: handlePreprocessing(); break;
    case STATE_INFERENCE: handleInference(); break;
    case STATE_SHOW_RESULT: handleShowResult(now); break;
    case STATE_WAIT_REMOVAL: handleWaitRemoval(now); break;
    case STATE_ERROR: handleError(now); break;
  }
}

// ---- IDLE: LED tắt, chỉ theo dõi cảm biến; không chụp, không inference ------------------------
void SystemController::handleIdle(uint32_t now) {
  if (stateEntry_) {
    stateEntry_ = false;
    if (leds_.mode() != LED_MODE_OFF) leds_.allOff();
  }
  static uint32_t lastDbgMs = 0;
  if (now - lastDbgMs >= 1000) {
    lastDbgMs = now;
    Serial.printf("[SENSOR] GPIO%d level = %d\n", SENSOR_PIN, digitalRead(SENSOR_PIN));
  }
  if (manualPending_) {
    manualPending_ = false;
    startCycle(true);
  } else if (sensor_.isPresent()) {
    startCycle(false);
  }
}

void SystemController::startCycle(bool manual) {
  manualCycle_ = manual;
  detectMs_ = millis();
  detectUs_ = micros();
  ++stats_.triggers;
  // Kiểm chứng chống lặp: mỗi chu trình (từ cảm biến) phải ứng với một lần "vật xuất hiện" mới.
  if (!manual) {
    if (!firstTrigger_ && sensor_.arrivalCount() == arrivalsAtLastTrigger_) {
      ++stats_.duplicateTriggers;
      Serial.println("[SYSTEM] WARNING: trigger without a new sensor arrival (duplicate) - please report");
    }
    arrivalsAtLastTrigger_ = sensor_.arrivalCount();
    firstTrigger_ = false;
  }
  releasesAtTrigger_ = sensor_.releaseCount();
  Serial.println("------------------------------------");
  if (manual) Serial.println("[SENSOR] Manual trigger (Serial command 'm') - sensor bypassed for this cycle");
  else Serial.printf("[SENSOR] Potato detected (cycle #%lu)\n", (unsigned long)stats_.triggers);
  enterState(STATE_OBJECT_DETECTED);
}

// ---- OBJECT_DETECTED: chờ vật ổn định rồi chụp; vật rời đi sớm → huỷ chu trình -----------------
void SystemController::handleObjectDetected(uint32_t now) {
  if (stateEntry_) {
    stateEntry_ = false;
    leds_.allOff();
  }
  if (!manualCycle_ && !sensor_.isPresent()) {
    ++stats_.abortedBeforeCapture;
    Serial.println("[SENSOR] Object left before capture -> cycle cancelled (no classification)");
    enterState(STATE_WAIT_REMOVAL);
    return;
  }
  if (now - stateSinceMs_ >= CAMERA_TRIGGER_DELAY_MS) enterState(STATE_CAMERA_CAPTURE);
}

// ---- CAMERA_CAPTURE ---------------------------------------------------------------------------
void SystemController::handleCapture() {
  stateEntry_ = false;
  resetResult(result_);
  leds_.allOff();
  Serial.println();
  Serial.println("[CAMERA] Capturing image...");
  cycleStartUs_ = micros();
  uint32_t us = 0;
  if (faultCaptureNext_) {
    // Kiểm thử TC08: giả lập lỗi chụp, đi qua đúng nhánh báo lỗi runtime (không re-init camera thật).
    faultCaptureNext_ = false;
    ++stats_.captureErrors;
    Serial.println("[TEST] Injected fault: camera capture failure");
    Serial.println("[CAMERA] ERROR: capture failed (injected)");
    enterError(SYS_ERR_CAPTURE, false, true);
    return;
  }
  if (!camera_.capture(&us)) {
    ++stats_.captureErrors;
    Serial.printf("[CAMERA] ERROR: capture failed after %d attempts: %s (%lu consecutive)\n", CAMERA_CAPTURE_RETRIES,
                  CameraModule::errorString(camera_.lastError()), (unsigned long)camera_.consecutiveFailures());
    if (camera_.consecutiveFailures() >= CAMERA_FAILS_BEFORE_REINIT) {
      if (camera_.reinitCount() < CAMERA_MAX_REINIT) {
        if (!camera_.reinit()) {
          enterError(SYS_ERR_CAMERA_INIT, true, true);
          return;
        }
      } else {
        enterError(SYS_ERR_CAMERA_FAILING, true, false);
        return;
      }
    }
    enterError(SYS_ERR_CAPTURE, false, true);
    return;
  }
  result_.captureTimeUs = us;
  result_.captureTimeMs = (us + 500) / 1000;
  const camera_fb_t* fb = camera_.frame();
  Serial.printf("[CAMERA] Capture successful (%ux%u, %s, %u B, %.1f ms)\n", (unsigned)fb->width,
                (unsigned)fb->height, fb->format == PIXFORMAT_JPEG ? "JPEG" : "RGB565", (unsigned)fb->len, msf(us));
  if (dumpFrameNext_) {
    dumpFrameNext_ = false;
    char header[96];
    snprintf(header, sizeof(header), "seq=%lu w=%u h=%u format=%s", (unsigned long)(stats_.triggers),
             (unsigned)fb->width, (unsigned)fb->height, fb->format == PIXFORMAT_JPEG ? "JPEG" : "RGB565_BE");
    dumpBase64Block(Serial, "FRAME", header, fb->buf, fb->len);
  }
  enterState(STATE_PREPROCESSING);
}

// ---- PREPROCESSING: RGB565 → RGB888 → framing → bilinear 96x96 → INT8 vào input tensor ----------
void SystemController::handlePreprocessing() {
  stateEntry_ = false;
  ImageView img;
  uint32_t decodeUs = 0;
  if (!camera_.getImage(&img, &decodeUs)) {
    camera_.release();
    ++stats_.preprocessErrors;
    Serial.printf("[IMAGE] ERROR: cannot read frame: %s\n", CameraModule::errorString(camera_.lastError()));
    enterError(SYS_ERR_IMAGE, false, true);
    return;
  }
  Serial.printf("[IMAGE] Original: %dx%d (%s)\n", img.width, img.height, pixelLayoutName(img.layout));
  PreprocStats st = {};
  const bool ok = cnn::preprocess(img, PREPROC_FRAMING, &st);
  if (!ok) {
    camera_.release();
    ++stats_.preprocessErrors;
    enterError(SYS_ERR_PREPROCESS, false, true);
    return;
  }
  result_.preprocessTimeUs = decodeUs + st.elapsedUs;
  result_.preprocessTimeMs = (result_.preprocessTimeUs + 500) / 1000;
  result_.inputCrc32 = st.tensorCrc32;
  if (st.framing == FRAMING_LETTERBOX) {
    Serial.printf("[IMAGE] Framing: LETTERBOX -> %dx%d, pad RGB(%u,%u,%u)\n", st.virtualWidth, st.virtualHeight,
                  st.padColor[0], st.padColor[1], st.padColor[2]);
  } else {
    Serial.printf("[IMAGE] Framing: %s -> %dx%d\n", framingName(st.framing), st.virtualWidth, st.virtualHeight);
  }
  Serial.printf("[IMAGE] Resized: %dx%d (bilinear, half-pixel centers)\n", cnn::inputWidth(), cnn::inputHeight());
  Serial.printf("[IMAGE] Quantization complete: q = round(x / %.6f + %ld), tensor CRC32 %08lx\n", cnn::inputScale(),
                (long)cnn::inputZeroPoint(), (unsigned long)st.tensorCrc32);
  // Xuất tensor TRƯỚC Invoke(): TFLM có thể dùng lại vùng nhớ input trong lúc chạy.
  if (dumpTensorNext_ || DUMP_TENSOR_EVERY_CYCLE) {
    dumpTensorNext_ = false;
    char header[128];
    snprintf(header, sizeof(header), "seq=%lu shape=1x%dx%dx3 dtype=int8 scale=%.8f zero_point=%ld framing=%s",
             (unsigned long)stats_.triggers, cnn::inputHeight(), cnn::inputWidth(), cnn::inputScale(),
             (long)cnn::inputZeroPoint(), framingName(st.framing));
    dumpBase64Block(Serial, "TENSOR", header, reinterpret_cast<const uint8_t*>(cnn::inputData()),
                    cnn::inputBytes());
  }
  enterState(STATE_INFERENCE);
}

// ---- INFERENCE ---------------------------------------------------------------------------------
void SystemController::handleInference() {
  stateEntry_ = false;
  Serial.println();
  Serial.println("[CNN] Running inference...");
  if (!cnn::run(result_)) {
    camera_.release();
    ++stats_.inferenceErrors;
    enterError(result_.errorCode == CNN_ERR_OUTPUT_INVALID ? SYS_ERR_OUTPUT : SYS_ERR_INFERENCE, false, true);
    return;
  }
  ++stats_.classifications;
  if (result_.predictedClass == POTATO_GOOD) ++stats_.good;
  else if (result_.predictedClass == POTATO_NOT_GOOD) ++stats_.notGood;
  else ++stats_.unknown;
  if (stats_.inferenceUsMin == 0 || result_.inferenceTimeUs < stats_.inferenceUsMin)
    stats_.inferenceUsMin = result_.inferenceTimeUs;
  if (result_.inferenceTimeUs > stats_.inferenceUsMax) stats_.inferenceUsMax = result_.inferenceTimeUs;
  stats_.inferenceUsSum += result_.inferenceTimeUs;

  Serial.println();
  Serial.printf("[RESULT] %s\n", potatoClassName(result_.predictedClass));
  Serial.printf("[RESULT] Good: %.1f%%\n", result_.goodProbability * 100.0f);
  Serial.printf("[RESULT] Not Good: %.1f%%\n", result_.notGoodProbability * 100.0f);
  Serial.printf("[RESULT] Raw int8 output: [%d, %d] (scale %.8f, zero_point %ld)\n", result_.rawGood,
                result_.rawNotGood, cnn::outputScale(), (long)cnn::outputZeroPoint());
  if (result_.tie) Serial.println("[RESULT] TIE: both scores equal -> argmax picks index 0 (good), same as np.argmax");
  if (result_.belowThreshold)
    Serial.printf("[RESULT] Confidence %.1f%% < threshold %.1f%% -> UNKNOWN\n", result_.confidence * 100.0f,
                  CONFIDENCE_THRESHOLD * 100.0f);
  enterState(STATE_SHOW_RESULT);
}

void SystemController::printTimings() {
  const uint32_t ledUs = micros();
  result_.totalTimeUs = ledUs - cycleStartUs_;
  result_.totalTimeMs = (result_.totalTimeUs + 500) / 1000;
  Serial.println();
  Serial.printf("[TIME] Capture: %.1f ms\n", msf(result_.captureTimeUs));
  Serial.printf("[TIME] Preprocessing: %.1f ms\n", msf(result_.preprocessTimeUs));
  Serial.printf("[TIME] Inference: %.1f ms\n", msf(result_.inferenceTimeUs));
  Serial.printf("[TIME] Total (capture -> LED): %.1f ms\n", msf(result_.totalTimeUs));
  if (!manualCycle_) Serial.printf("[TIME] Sensor -> LED (incl. %d ms trigger delay): %.1f ms\n",
                                   CAMERA_TRIGGER_DELAY_MS, msf(ledUs - detectUs_));
  // Dòng máy đọc được (tools/serial_compare.py)
  Serial.printf("#RESULT,seq=%lu,pred=%s,raw_good=%d,raw_not_good=%d,p_good=%.6f,p_not_good=%.6f,"
                "capture_ms=%.2f,pre_ms=%.2f,inf_ms=%.2f,total_ms=%.2f,input_crc32=%08lx\n",
                (unsigned long)stats_.triggers, potatoClassName(result_.predictedClass), result_.rawGood,
                result_.rawNotGood, result_.goodProbability, result_.notGoodProbability,
                msf(result_.captureTimeUs), msf(result_.preprocessTimeUs), msf(result_.inferenceTimeUs),
                msf(result_.totalTimeUs), (unsigned long)result_.inputCrc32);
}

// ---- SHOW_RESULT: LED theo kết quả trong LED_RESULT_DURATION_MS --------------------------------
void SystemController::handleShowResult(uint32_t now) {
  if (stateEntry_) {
    stateEntry_ = false;
    Serial.println();
    if (result_.predictedClass == POTATO_GOOD) leds_.showGood();
    else if (result_.predictedClass == POTATO_NOT_GOOD) leds_.showNotGood();
    else {
      leds_.allOff();
      Serial.println("[LED] UNKNOWN -> no quality LED");
    }
    printTimings();
    const uint32_t resultUptimeMs = millis();
    net_.queueResult(result_, resultUptimeMs);  // MQTT nhận cùng khóa sự kiện với ảnh.
    if (net_.uploadImage(camera_.frame(), result_.sequence, resultUptimeMs)) {
      Serial.println("[NET] Classification photo upload started");
    }
    camera_.release();
#if LOG_MEMORY_EVERY_CYCLE
    printMemoryInfo("MEMORY");
#endif
    stats_.memLast = takeMemorySnapshot();
    if (stats_.classifications == 1) stats_.memAfterFirstCycle = stats_.memLast;
    Serial.println();
  }
  if (millis() - stateSinceMs_ >= LED_RESULT_DURATION_MS) {
    leds_.allOff();
    enterState(STATE_WAIT_REMOVAL);
  }
  (void)now;  // dùng millis(): khối on-entry ở trên có thể chạy lâu (log), now đã cũ
}

// ---- WAIT_REMOVAL: không phân loại lại; chờ vật rời đi ổn định + cooldown ---------------------
void SystemController::handleWaitRemoval(uint32_t now) {
  if (stateEntry_) {
    stateEntry_ = false;
    removalLogged_ = false;
    stuckWarned_ = false;
    if (leds_.mode() != LED_MODE_OFF) leds_.allOff();
    if (sensor_.isPresent()) Serial.println("[SENSOR] Waiting for removal...");
  }

  if (sensor_.isPresent()) {
    if (!stuckWarned_ && sensor_.presentForMs(now) >= SENSOR_STUCK_WARNING_MS) {
      stuckWarned_ = true;
      Serial.printf("[SENSOR] WARNING: sensor active for %lu s - vat bi ket hoac cam bien chinh sai/bi nhieu. "
                    "Khong chup lai cho den khi vat roi di.\n",
                    (unsigned long)(sensor_.presentForMs(now) / 1000));
    }
#if SENSOR_COOLDOWN_REQUIRES_CLEAR
    if (removalLogged_) {
      // Vật quay lại trong cooldown → coi là cùng một vật (thường do tín hiệu chập chờn).
      removalLogged_ = false;
      ++stats_.reDetectionsInCooldown;
      Serial.println("[SENSOR] Re-detected during cooldown -> treated as the same object (no new classification)");
    }
    return;
#else
    // Chế độ cho phép vật mới xuất hiện trong cooldown: chỉ cần đã thấy một lần "rời đi".
    if (sensor_.releaseCount() != releasesAtTrigger_ && now - sensor_.lastReleaseMs() >= SENSOR_COOLDOWN_MS) {
      Serial.println("[SYSTEM] READY FOR NEXT POTATO");
      Serial.println("------------------------------------");
      enterState(STATE_IDLE);
    }
    return;
#endif
  }

  if (!removalLogged_) {
    removalLogged_ = true;
    Serial.println("[SENSOR] Object removed");
  }
  if (sensor_.absentForMs(now) >= SENSOR_COOLDOWN_MS) {
    Serial.println("[SYSTEM] READY FOR NEXT POTATO");
    Serial.println("------------------------------------");
    enterState(STATE_IDLE);
  }
}

// =============================================================================================
// Lỗi
// =============================================================================================
void SystemController::enterError(SystemError e, bool fatal, bool recoverable) {
  camera_.release();  // không giữ frame buffer khi lỗi
  error_ = e;
  errorFatal_ = fatal;
  errorRecoverable_ = recoverable;
  lastRetryMs_ = millis();
  lastFatalLogMs_ = millis();
  result_.success = false;
  result_.predictedClass = POTATO_UNKNOWN;
  Serial.printf("[ERROR] %s (%s%s)\n", errorName(e), fatal ? "system" : "runtime",
                fatal ? (recoverable ? ", limited retries" : ", not recoverable -> safe state") : ", cycle aborted");
  Serial.println("[ERROR] No GOOD/NOT_GOOD result is shown for this event.");
  leds_.showError();  // khác hẳn NOT_GOOD (đỏ sáng liên tục)
  enterState(STATE_ERROR);
}

bool SystemController::retryInit() {
  switch (error_) {
    case SYS_ERR_CAMERA_INIT:
      if (!camera_.begin()) return false;
      break;
    case SYS_ERR_MODEL_INIT:
      if (!cnn::begin()) return false;
      break;
    default:
      return false;
  }
  // Đảm bảo thành phần còn lại cũng đã sẵn sàng.
  if (!camera_.isReady() && !camera_.begin()) {
    error_ = SYS_ERR_CAMERA_INIT;
    return false;
  }
  if (!cnn::isReady() && !cnn::begin()) {
    error_ = SYS_ERR_MODEL_INIT;
    return false;
  }
  return true;
}

void SystemController::handleError(uint32_t now) {
  stateEntry_ = false;
  if (!errorFatal_) {
    // Lỗi trong một chu trình: báo lỗi một lúc rồi chờ vật rời đi (không thử lại trên cùng vật).
    if (now - stateSinceMs_ >= ERROR_DISPLAY_MS) {
      leds_.allOff();
      enterState(STATE_WAIT_REMOVAL);
    }
    return;
  }
  if (errorRecoverable_ && initRetries_ < INIT_MAX_RETRIES && now - lastRetryMs_ >= INIT_RETRY_DELAY_MS) {
    ++initRetries_;
    lastRetryMs_ = now;
    Serial.printf("[SYSTEM] Recovery attempt %lu/%d for: %s\n", (unsigned long)initRetries_, INIT_MAX_RETRIES,
                  errorName(error_));
    if (retryInit()) {
      Serial.println("[SYSTEM] Recovery successful");
      error_ = SYS_OK;
      errorFatal_ = false;
      leds_.allOff();
      goReady();
    }
    return;
  }
  if (now - lastFatalLogMs_ >= FATAL_LOG_INTERVAL_MS) {
    lastFatalLogMs_ = now;
    Serial.printf("[ERROR] SAFE STATE: %s. Classification disabled; fix the problem and press RESET.\n",
                  errorName(error_));
  }
}

// =============================================================================================
// Lệnh Serial & thống kê
// =============================================================================================
bool SystemController::manualTrigger() {
  if (state_ != STATE_IDLE) {
    Serial.printf("[CMD] Manual trigger ignored: state is %s (only allowed in IDLE)\n", stateName(state_));
    return false;
  }
  manualPending_ = true;
  return true;
}

void SystemController::resetStats() {
  stats_ = SystemStats{};
  Serial.println("[CMD] Statistics reset");
}

void SystemController::printStatus() {
  const uint32_t n = stats_.classifications;
  Serial.println("========== STATUS ==========");
  Serial.printf("State: %s, uptime %lu s, last error: %s\n", stateName(state_), (unsigned long)(millis() / 1000),
                errorName(error_));
  Serial.printf("Sensor: raw level=%d present=%d arrivals=%lu releases=%lu glitches=%lu\n", sensor_.rawLevel(),
                sensor_.isPresent(), (unsigned long)sensor_.arrivalCount(), (unsigned long)sensor_.releaseCount(),
                (unsigned long)sensor_.glitchCount());
  Serial.printf("Cycles: triggers=%lu classified=%lu (GOOD %lu, NOT_GOOD %lu, UNKNOWN %lu)\n",
                (unsigned long)stats_.triggers, (unsigned long)n, (unsigned long)stats_.good,
                (unsigned long)stats_.notGood, (unsigned long)stats_.unknown);
  Serial.printf("Aborted before capture=%lu, re-detections in cooldown=%lu, duplicate triggers=%lu\n",
                (unsigned long)stats_.abortedBeforeCapture, (unsigned long)stats_.reDetectionsInCooldown,
                (unsigned long)stats_.duplicateTriggers);
  Serial.printf("Errors: capture=%lu preprocess=%lu inference=%lu, camera re-inits=%lu\n",
                (unsigned long)stats_.captureErrors, (unsigned long)stats_.preprocessErrors,
                (unsigned long)stats_.inferenceErrors, (unsigned long)camera_.reinitCount());
  if (n > 0) {
    Serial.printf("Inference time: min %.1f ms, avg %.1f ms, max %.1f ms\n", msf(stats_.inferenceUsMin),
                  (double)stats_.inferenceUsSum / n / 1000.0, msf(stats_.inferenceUsMax));
    const MemorySnapshot now = takeMemorySnapshot();
    Serial.printf("Internal RAM free: after 1st cycle %lu B, now %lu B (delta %ld B)\n",
                  (unsigned long)stats_.memAfterFirstCycle.internalFree, (unsigned long)now.internalFree,
                  (long)now.internalFree - (long)stats_.memAfterFirstCycle.internalFree);
    Serial.printf("PSRAM free:        after 1st cycle %lu B, now %lu B (delta %ld B)\n",
                  (unsigned long)stats_.memAfterFirstCycle.psramFree, (unsigned long)now.psramFree,
                  (long)now.psramFree - (long)stats_.memAfterFirstCycle.psramFree);
  }
  net_.printStatus();
  cnn::printSummary();
  printMemoryInfo("MEMORY");
  Serial.println("============================");
}

void SystemController::pollSerialCommands() {
#if ENABLE_SERIAL_COMMANDS
  while (Serial.available() > 0) {
    const int c = Serial.read();
    switch (c) {
      case 'h':
      case '?':
        Serial.println("[CMD] h=help  s=status  t=dump input tensor of next cycle  f=dump camera frame of next "
                       "cycle  m=manual trigger (IDLE only)  r=reset stats");
        Serial.println("[CMD] test faults for the next cycle: 1=capture failure  2=Invoke failure  3=invalid output");
        break;
      case '1':
        faultCaptureNext_ = true;
        Serial.println("[CMD] Next cycle: injected camera capture failure (TC08)");
        break;
      case '2':
        cnn::injectFaultForTest(cnn::FAULT_INVOKE);
        Serial.println("[CMD] Next cycle: injected Invoke() failure (TC09)");
        break;
      case '3':
        cnn::injectFaultForTest(cnn::FAULT_OUTPUT);
        Serial.println("[CMD] Next cycle: injected invalid output tensor (TC13)");
        break;
      case 's': printStatus(); break;
      case 't':
        requestTensorDump();
        Serial.println("[CMD] Input tensor of the next classification will be dumped (BEGIN_TENSOR ... END_TENSOR)");
        break;
      case 'f':
        requestFrameDump();
        Serial.println("[CMD] Next camera frame will be dumped (BEGIN_FRAME ... END_FRAME)");
        break;
      case 'm':
        if (manualTrigger()) Serial.println("[CMD] Manual trigger accepted");
        break;
      case 'r': resetStats(); break;
      default: break;  // bỏ qua '\r', '\n' và ký tự lạ
    }
  }
#endif
}

const char* SystemController::stateName(SystemState s) {
  switch (s) {
    case STATE_INIT: return "INIT";
    case STATE_IDLE: return "IDLE";
    case STATE_OBJECT_DETECTED: return "OBJECT_DETECTED";
    case STATE_CAMERA_CAPTURE: return "CAMERA_CAPTURE";
    case STATE_PREPROCESSING: return "PREPROCESSING";
    case STATE_INFERENCE: return "INFERENCE";
    case STATE_SHOW_RESULT: return "SHOW_RESULT";
    case STATE_WAIT_REMOVAL: return "WAIT_REMOVAL";
    case STATE_ERROR: return "ERROR";
  }
  return "?";
}

const char* SystemController::errorName(SystemError e) {
  switch (e) {
    case SYS_OK: return "none";
    case SYS_ERR_PSRAM: return "PSRAM not available";
    case SYS_ERR_CAMERA_INIT: return "camera initialization failed";
    case SYS_ERR_MODEL_INIT: return "CNN/TFLM initialization failed";
    case SYS_ERR_CAPTURE: return "camera capture failed";
    case SYS_ERR_CAMERA_FAILING: return "camera keeps failing after re-init";
    case SYS_ERR_IMAGE: return "invalid image";
    case SYS_ERR_PREPROCESS: return "image preprocessing failed";
    case SYS_ERR_INFERENCE: return "TFLM Invoke failed";
    case SYS_ERR_OUTPUT: return "invalid output tensor values";
  }
  return "?";
}
