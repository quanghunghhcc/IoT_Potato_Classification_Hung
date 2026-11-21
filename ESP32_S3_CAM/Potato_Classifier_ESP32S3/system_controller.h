// system_controller.h — State machine điều phối: cảm biến → camera → tiền xử lý → CNN → LED.
//
// Chống phân loại lặp: chỉ STATE_IDLE mới bắt đầu chu trình mới, và chỉ quay về IDLE từ
// STATE_WAIT_REMOVAL sau khi cảm biến xác nhận vật đã rời đi (debounce) + cooldown. Vì vậy một vật
// nằm yên trước cảm biến chỉ được phân loại đúng một lần.
#pragma once

#include <Arduino.h>

#include "camera_module.h"
#include "cnn_inference.h"
#include "diagnostics.h"
#include "led_control.h"
#include "network_reporter.h"
#include "sensor_module.h"

enum SystemState : uint8_t {
  STATE_INIT = 0,
  STATE_IDLE,
  STATE_OBJECT_DETECTED,
  STATE_CAMERA_CAPTURE,
  STATE_PREPROCESSING,
  STATE_INFERENCE,
  STATE_SHOW_RESULT,
  STATE_WAIT_REMOVAL,
  STATE_ERROR,
};

enum SystemError : uint8_t {
  SYS_OK = 0,
  SYS_ERR_PSRAM,
  SYS_ERR_CAMERA_INIT,
  SYS_ERR_MODEL_INIT,
  SYS_ERR_CAPTURE,
  SYS_ERR_CAMERA_FAILING,
  SYS_ERR_IMAGE,
  SYS_ERR_PREPROCESS,
  SYS_ERR_INFERENCE,
  SYS_ERR_OUTPUT,
};

struct SystemStats {
  uint32_t triggers;              // chu trình bắt đầu (vật được xác nhận)
  uint32_t classifications;       // phân loại thành công
  uint32_t good;
  uint32_t notGood;
  uint32_t unknown;
  uint32_t abortedBeforeCapture;  // vật rời đi trước khi chụp
  uint32_t captureErrors;
  uint32_t preprocessErrors;
  uint32_t inferenceErrors;
  uint32_t duplicateTriggers;     // chu trình bắt đầu mà không có lần "vật xuất hiện" mới — phải luôn = 0
  uint32_t reDetectionsInCooldown;
  uint32_t inferenceUsMin;
  uint32_t inferenceUsMax;
  uint64_t inferenceUsSum;
  MemorySnapshot memAfterFirstCycle;
  MemorySnapshot memLast;
};

class SystemController {
 public:
  bool begin();                    // STATE_INIT: PSRAM, LED, cảm biến, camera, TFLM
  void update();                   // gọi liên tục trong loop(), không chặn (trừ chụp/inference)
  void pollSerialCommands();       // lệnh 1 ký tự qua Serial Monitor

  SystemState state() const { return state_; }
  const SystemStats& stats() const { return stats_; }
  const ClassificationResult& lastResult() const { return result_; }
  void printStatus();
  void resetStats();
  static const char* stateName(SystemState s);
  static const char* errorName(SystemError e);

  void requestTensorDump() { dumpTensorNext_ = true; }
  void requestFrameDump() { dumpFrameNext_ = true; }
  bool manualTrigger();            // giả lập một vật (chỉ khi IDLE) để thử camera + CNN không cần cảm biến

 private:
  void enterState(SystemState s);
  void enterError(SystemError e, bool fatal, bool recoverable);
  void goReady();
  bool retryInit();
  void startCycle(bool manual);

  void handleIdle(uint32_t now);
  void handleObjectDetected(uint32_t now);
  void handleCapture();
  void handlePreprocessing();
  void handleInference();
  void handleShowResult(uint32_t now);
  void handleWaitRemoval(uint32_t now);
  void handleError(uint32_t now);

  void printBanner();
  void printTimings();

  SensorModule sensor_;
  CameraModule camera_;
  LedControl leds_;
  NetworkReporter net_;
  ClassificationResult result_;

  SystemState state_ = STATE_INIT;
  uint32_t stateSinceMs_ = 0;
  bool stateEntry_ = true;

  SystemError error_ = SYS_OK;
  bool errorFatal_ = false;
  bool errorRecoverable_ = false;
  uint32_t initRetries_ = 0;
  uint32_t lastRetryMs_ = 0;
  uint32_t lastFatalLogMs_ = 0;

  bool manualCycle_ = false;
  bool manualPending_ = false;
  uint32_t detectMs_ = 0;
  uint32_t cycleStartUs_ = 0;
  uint32_t detectUs_ = 0;
  uint32_t arrivalsAtLastTrigger_ = 0;
  uint32_t releasesAtTrigger_ = 0;
  bool firstTrigger_ = true;
  bool removalLogged_ = false;
  bool stuckWarned_ = false;
  bool firstReady_ = true;

  bool dumpTensorNext_ = false;
  bool dumpFrameNext_ = false;
  bool faultCaptureNext_ = false;  // chèn lỗi chụp ảnh cho TC08 (lệnh '1')
  SystemStats stats_ = {};
};
