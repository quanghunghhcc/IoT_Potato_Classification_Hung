// cnn_inference.cpp — TensorFlow Lite Micro (esp-tflite-micro tích hợp sẵn trong Arduino-ESP32 Core 3.x)
//
// Thư viện TFLM của core luôn được biên dịch với TF_LITE_STATIC_MEMORY (cờ này thay đổi layout của
// struct TfLiteTensor). Core truyền cờ qua build flags của nó; khối dưới đây chỉ là bảo hiểm phòng khi
// cờ bị thiếu, và PHẢI đứng trước mọi header TensorFlow.
#ifndef TF_LITE_STATIC_MEMORY
#define TF_LITE_STATIC_MEMORY
#endif

#include <math.h>
#include <string.h>

#include <new>

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#ifndef TFLITE_SCHEMA_VERSION
#define TFLITE_SCHEMA_VERSION 3
#endif

// Header Arduino/ESP-IDF đặt SAU header TFLM để tránh xung đột macro.
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>

#include "cnn_inference.h"
#include "config.h"
#include "diagnostics.h"
#include "model_data.h"

namespace {

// Danh sách toán tử lấy từ FlatBuffer thật của potato_int8.tflite (tools/inspect_model.py):
// CONV_2D v3, MAX_POOL_2D v2, DEPTHWISE_CONV_2D v3, MEAN v2, FULLY_CONNECTED v4, SOFTMAX v2.
constexpr int kOpCount = 6;
using Resolver = tflite::MicroMutableOpResolver<kOpCount>;

alignas(Resolver) uint8_t gResolverStorage[sizeof(Resolver)];
alignas(tflite::MicroInterpreter) uint8_t gInterpreterStorage[sizeof(tflite::MicroInterpreter)];

const tflite::Model* gModel = nullptr;
Resolver* gResolver = nullptr;
tflite::MicroInterpreter* gInterpreter = nullptr;
uint8_t* gArena = nullptr;
size_t gArenaSize = 0;
bool gArenaPsram = false;
TfLiteTensor* gInput = nullptr;
TfLiteTensor* gOutput = nullptr;
bool gReady = false;
CnnError gError = CNN_ERR_NOT_INITIALIZED;
uint32_t gSequence = 0;
uint32_t gModelCrc = 0;
cnn::TestFault gFault = cnn::FAULT_NONE;

bool fail(CnnError e, const char* message) {
  gError = e;
  gReady = false;
  Serial.printf("[MODEL] ERROR: %s (%s)\n", message, cnn::errorString(e));
  return false;
}

void printShape(const char* label, const TfLiteTensor* t) {
  Serial.printf("[MODEL] %s shape: [", label);
  for (int i = 0; i < t->dims->size; ++i) Serial.printf("%s%d", i ? "," : "", t->dims->data[i]);
  Serial.println("]");
}

bool shapeIs(const TfLiteTensor* t, const int* expected, int n) {
  if (t->dims == nullptr || t->dims->size != n) return false;
  for (int i = 0; i < n; ++i)
    if (t->dims->data[i] != expected[i]) return false;
  return true;
}

bool allocateArena() {
  if (gArena != nullptr) return true;  // cấp phát một lần, dùng lại suốt vòng đời firmware
  const size_t size = TENSOR_ARENA_SIZE;
  [[maybe_unused]] const uint32_t internalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  [[maybe_unused]] const uint32_t psramCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#if ARENA_LOCATION == ARENA_IN_INTERNAL
  gArena = static_cast<uint8_t*>(heap_caps_aligned_alloc(16, size, internalCaps));
  gArenaPsram = false;
#elif ARENA_LOCATION == ARENA_AUTO
  if (heap_caps_get_largest_free_block(internalCaps) >= size + ARENA_INTERNAL_RESERVE) {
    gArena = static_cast<uint8_t*>(heap_caps_aligned_alloc(16, size, internalCaps));
    gArenaPsram = false;
  }
  if (gArena == nullptr && psramFound()) {
    gArena = static_cast<uint8_t*>(heap_caps_aligned_alloc(16, size, psramCaps));
    gArenaPsram = true;
  }
#else
  if (psramFound()) {
    gArena = static_cast<uint8_t*>(heap_caps_aligned_alloc(16, size, psramCaps));
    gArenaPsram = true;
  }
#endif
  if (gArena == nullptr) return false;
  gArenaSize = size;
  Serial.printf("[MODEL] Tensor arena: %u B allocated in %s at %p (16-byte aligned)\n", (unsigned)gArenaSize,
                gArenaPsram ? "PSRAM" : "internal RAM", gArena);
  return true;
}

}  // namespace

// =============================================================================================

void resetResult(ClassificationResult& r) {
  memset(&r, 0, sizeof(r));
  r.success = false;
  r.predictedClass = POTATO_UNKNOWN;
}

const char* potatoClassName(PotatoClass c) {
  switch (c) {
    case POTATO_GOOD: return "GOOD";
    case POTATO_NOT_GOOD: return "NOT_GOOD";
    case POTATO_UNKNOWN: return "UNKNOWN";
  }
  return "INVALID";
}

namespace cnn {

bool begin() {
  gReady = false;
  gError = CNN_ERR_NOT_INITIALIZED;
  Serial.println("[MODEL] Initializing TFLite Micro...");

  // ---- 1. Kiểm tra dữ liệu model nhúng ---------------------------------------------------------
  const uint8_t* data = g_potato_model;
  const size_t len = g_potato_model_len;
  Serial.printf("[MODEL] Model data: %u B at %p (%s)\n", (unsigned)len, data,
                esp_ptr_external_ram(data) ? "PSRAM"
                : esp_ptr_in_drom(data)    ? "flash, memory-mapped (no RAM copy)"
                                           : "RAM");
  if (reinterpret_cast<uintptr_t>(data) % 16 != 0) return fail(CNN_ERR_MODEL_MISALIGNED, "model array is not 16-byte aligned");
  if (len != POTATO_MODEL_EXPECTED_LEN) {
    Serial.printf("[MODEL] size %u != expected %u\n", (unsigned)len, (unsigned)POTATO_MODEL_EXPECTED_LEN);
    return fail(CNN_ERR_MODEL_SIZE, "model size mismatch (chay tools/sync_project.py)");
  }
  gModelCrc = crc32Update(0, data, len);
  Serial.printf("[MODEL] CRC32: %08lx (expected %08lx)\n", (unsigned long)gModelCrc,
                (unsigned long)POTATO_MODEL_EXPECTED_CRC32);
#if MODEL_ENFORCE_CRC
  if (gModelCrc != POTATO_MODEL_EXPECTED_CRC32) return fail(CNN_ERR_MODEL_CRC, "model CRC32 mismatch");
#endif
  if (len < 8 || memcmp(data + 4, "TFL3", 4) != 0) return fail(CNN_ERR_MODEL_IDENTIFIER, "missing TFL3 identifier");
  {
    flatbuffers::Verifier verifier(data, len);
    if (!tflite::VerifyModelBuffer(verifier)) return fail(CNN_ERR_MODEL_VERIFY, "FlatBuffer verification failed");
  }
  gModel = tflite::GetModel(data);
  Serial.printf("[MODEL] Schema version: %lu (runtime supports %d)\n", (unsigned long)gModel->version(),
                (int)TFLITE_SCHEMA_VERSION);
  if (gModel->version() != TFLITE_SCHEMA_VERSION) return fail(CNN_ERR_SCHEMA_VERSION, "schema version mismatch");
  if (gModel->subgraphs() == nullptr || gModel->subgraphs()->size() != 1)
    return fail(CNN_ERR_SUBGRAPH, "expected exactly one subgraph");
  Serial.println("[MODEL] Model loaded");

  // ---- 2. Đăng ký đúng các toán tử mô hình dùng ------------------------------------------------
  if (gResolver != nullptr) gResolver->~Resolver();
  gResolver = new (gResolverStorage) Resolver();
  const bool registered = gResolver->AddConv2D() == kTfLiteOk && gResolver->AddDepthwiseConv2D() == kTfLiteOk &&
                          gResolver->AddMaxPool2D() == kTfLiteOk && gResolver->AddMean() == kTfLiteOk &&
                          gResolver->AddFullyConnected() == kTfLiteOk && gResolver->AddSoftmax() == kTfLiteOk;
  if (!registered) return fail(CNN_ERR_RESOLVER, "MicroMutableOpResolver registration failed");

  const auto* opcodes = gModel->operator_codes();
  if (opcodes == nullptr) return fail(CNN_ERR_MODEL_VERIFY, "model has no operator codes");
  bool allSupported = true;
  Serial.printf("[MODEL] Operators used by the model: %u\n", (unsigned)opcodes->size());
  for (uint32_t i = 0; i < opcodes->size(); ++i) {
    const tflite::OperatorCode* oc = opcodes->Get(i);
    const int deprecated = static_cast<int>(oc->deprecated_builtin_code());
    const int builtin = static_cast<int>(oc->builtin_code());
    const auto code = static_cast<tflite::BuiltinOperator>(builtin > deprecated ? builtin : deprecated);
    const bool custom = (code == tflite::BuiltinOperator_CUSTOM);
    const bool ok = !custom && gResolver->FindOp(code) != nullptr;
    const char* name = custom ? (oc->custom_code() ? oc->custom_code()->c_str() : "CUSTOM")
                              : tflite::EnumNameBuiltinOperator(code);
    Serial.printf("[MODEL]   %-18s version %d  -> %s\n", name, (int)oc->version(), ok ? "registered" : "NOT SUPPORTED");
    if (!ok) allSupported = false;
  }
  if (!allSupported) return fail(CNN_ERR_UNSUPPORTED_OP, "model uses an operator that is not registered");

  // ---- 3. Tensor arena (không đặt trên stack) --------------------------------------------------
  if (!allocateArena()) {
    Serial.printf("[MODEL] requested %u B, PSRAM free %lu B, internal largest block %lu B\n",
                  (unsigned)TENSOR_ARENA_SIZE, (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return fail(CNN_ERR_ARENA_ALLOC, "cannot allocate tensor arena");
  }

  // ---- 4. MicroInterpreter + AllocateTensors() -------------------------------------------------
  if (gInterpreter != nullptr) {
    gInterpreter->~MicroInterpreter();
    gInterpreter = nullptr;
  }
  gInterpreter = new (gInterpreterStorage) tflite::MicroInterpreter(gModel, *gResolver, gArena, gArenaSize);
  if (gInterpreter->initialization_status() != kTfLiteOk)
    return fail(CNN_ERR_INTERPRETER, "MicroInterpreter initialization failed");
  const TfLiteStatus alloc = gInterpreter->AllocateTensors();
  if (alloc != kTfLiteOk) {
    Serial.printf("[MODEL] AllocateTensors() returned %d with arena %u B -> tang TENSOR_ARENA_SIZE trong config.h\n",
                  (int)alloc, (unsigned)gArenaSize);
    return fail(CNN_ERR_ALLOCATE_TENSORS, "AllocateTensors failed; Invoke() will not be called");
  }
  Serial.println("[MODEL] Tensor allocation successful");
  Serial.printf("[MODEL] Tensor arena used: %u / %u B\n", (unsigned)gInterpreter->arena_used_bytes(),
                (unsigned)gArenaSize);

  // ---- 5. Kiểm tra input/output tensor --------------------------------------------------------
  if (gInterpreter->inputs_size() != 1 || gInterpreter->outputs_size() != 1)
    return fail(CNN_ERR_INPUT_TENSOR, "model must have exactly 1 input and 1 output");
  gInput = gInterpreter->input(0);
  gOutput = gInterpreter->output(0);
  if (gInput == nullptr || gOutput == nullptr) return fail(CNN_ERR_INPUT_TENSOR, "null input/output tensor");

  const int inShape[4] = {1, POTATO_INPUT_HEIGHT, POTATO_INPUT_WIDTH, POTATO_INPUT_CHANNELS};
  const int outShape[2] = {1, POTATO_NUM_CLASSES};
  printShape("Input", gInput);
  Serial.printf("[MODEL] Input type: %s\n", gInput->type == kTfLiteInt8 ? "INT8" : TfLiteTypeGetName(gInput->type));
  Serial.printf("[MODEL] Input quantization: scale=%.8f zero_point=%ld (bytes=%u)\n", gInput->params.scale,
                (long)gInput->params.zero_point, (unsigned)gInput->bytes);
  printShape("Output", gOutput);
  Serial.printf("[MODEL] Output type: %s\n", gOutput->type == kTfLiteInt8 ? "INT8" : TfLiteTypeGetName(gOutput->type));
  Serial.printf("[MODEL] Output quantization: scale=%.8f zero_point=%ld\n", gOutput->params.scale,
                (long)gOutput->params.zero_point);

  if (gInput->type != kTfLiteInt8 || !shapeIs(gInput, inShape, 4) ||
      gInput->bytes != static_cast<size_t>(POTATO_INPUT_HEIGHT * POTATO_INPUT_WIDTH * POTATO_INPUT_CHANNELS) ||
      gInput->data.int8 == nullptr || !(gInput->params.scale > 0.0f) || !isfinite(gInput->params.scale))
    return fail(CNN_ERR_INPUT_TENSOR, "input tensor is not INT8 [1,96,96,3] with a valid scale");
  if (gOutput->type != kTfLiteInt8 || !shapeIs(gOutput, outShape, 2) || gOutput->data.int8 == nullptr ||
      !(gOutput->params.scale > 0.0f) || !isfinite(gOutput->params.scale))
    return fail(CNN_ERR_OUTPUT_TENSOR, "output tensor is not INT8 [1,2] with a valid scale");

  // Đối chiếu với potato_metadata.json (chỉ cảnh báo: firmware luôn dùng giá trị đọc từ tensor).
  if (fabsf(gInput->params.scale - POTATO_META_INPUT_SCALE) > 1e-6f ||
      gInput->params.zero_point != POTATO_META_INPUT_ZERO_POINT ||
      fabsf(gOutput->params.scale - POTATO_META_OUTPUT_SCALE) > 1e-9f ||
      gOutput->params.zero_point != POTATO_META_OUTPUT_ZERO_POINT) {
    Serial.println("[MODEL] WARNING: tensor quantization differs from potato_metadata.json");
  }
  Serial.printf("[MODEL] Classes: 0=%s, 1=%s\n", POTATO_CLASS_NAME_0, POTATO_CLASS_NAME_1);

  gError = CNN_OK;
  gReady = true;
  return true;
}

bool isReady() { return gReady; }
CnnError lastError() { return gError; }

int8_t* inputData() { return gReady ? gInput->data.int8 : nullptr; }
size_t inputBytes() { return gReady ? gInput->bytes : 0; }
int inputWidth() { return POTATO_INPUT_WIDTH; }
int inputHeight() { return POTATO_INPUT_HEIGHT; }
int inputChannels() { return POTATO_INPUT_CHANNELS; }
float inputScale() { return gReady ? gInput->params.scale : 0.0f; }
int32_t inputZeroPoint() { return gReady ? gInput->params.zero_point : 0; }
float outputScale() { return gReady ? gOutput->params.scale : 0.0f; }
int32_t outputZeroPoint() { return gReady ? gOutput->params.zero_point : 0; }
size_t arenaSize() { return gArenaSize; }
size_t arenaUsedBytes() { return gInterpreter ? gInterpreter->arena_used_bytes() : 0; }
bool arenaInPsram() { return gArenaPsram; }

bool preprocess(const ImageView& image, int framing, PreprocStats* stats) {
  if (!gReady) {
    gError = CNN_ERR_NOT_INITIALIZED;
    return false;
  }
  const PreprocError e = preprocessToInt8(image, framing, gInput->data.int8, gInput->bytes, POTATO_INPUT_WIDTH,
                                          POTATO_INPUT_HEIGHT, gInput->params.scale, gInput->params.zero_point, stats);
  if (e != PREPROC_OK) {
    gError = CNN_ERR_PREPROCESS;
    Serial.printf("[IMAGE] ERROR: preprocessing failed: %s\n", preprocErrorString(e));
    return false;
  }
  return true;
}

bool run(ClassificationResult& r) {
  r.success = false;
  r.predictedClass = POTATO_UNKNOWN;
  r.tie = false;
  r.belowThreshold = false;
  if (!gReady) {
    r.errorCode = CNN_ERR_NOT_INITIALIZED;
    return false;
  }

  const TestFault fault = gFault;
  gFault = FAULT_NONE;
  if (fault != FAULT_NONE) Serial.printf("[TEST] Injected fault: %s\n", fault == FAULT_INVOKE ? "Invoke failure" : "invalid output");

  const uint32_t t0 = micros();
  const TfLiteStatus status = (fault == FAULT_INVOKE) ? kTfLiteError : gInterpreter->Invoke();
  r.inferenceTimeUs = micros() - t0;
  r.inferenceTimeMs = (r.inferenceTimeUs + 500) / 1000;
  if (status != kTfLiteOk) {
    gError = CNN_ERR_INVOKE;
    r.errorCode = CNN_ERR_INVOKE;
    Serial.printf("[CNN] ERROR: Invoke() failed with status %d\n", (int)status);
    return false;
  }

  gOutput = gInterpreter->output(0);
  const int8_t q0 = (fault == FAULT_OUTPUT) ? 127 : gOutput->data.int8[0];  // index 0 = good
  const int8_t q1 = (fault == FAULT_OUTPUT) ? 127 : gOutput->data.int8[1];  // index 1 = not_good
  const float scale = gOutput->params.scale;
  const int32_t zp = gOutput->params.zero_point;
  const float p0 = static_cast<float>(static_cast<int32_t>(q0) - zp) * scale;
  const float p1 = static_cast<float>(static_cast<int32_t>(q1) - zp) * scale;
  r.rawGood = q0;
  r.rawNotGood = q1;
  r.goodProbability = p0;
  r.notGoodProbability = p1;

  // Softmax sau giải lượng tử: mỗi giá trị trong [0,1] và tổng ≈ 1. Ngược lại = output không hợp lệ.
  if (!isfinite(p0) || !isfinite(p1) || p0 < -0.01f || p1 < -0.01f || p0 > 1.01f || p1 > 1.01f ||
      fabsf(p0 + p1 - 1.0f) > OUTPUT_SUM_TOLERANCE) {
    gError = CNN_ERR_OUTPUT_INVALID;
    r.errorCode = CNN_ERR_OUTPUT_INVALID;
    Serial.printf("[CNN] ERROR: invalid output raw=[%d,%d] -> p=[%.4f,%.4f] (sum %.4f)\n", q0, q1, p0, p1, p0 + p1);
    return false;
  }

  // Argmax giống np.argmax trong train.py: khi bằng nhau chọn chỉ số đầu tiên (0 = good).
  r.tie = (q0 == q1);
  r.predictedClass = (q0 >= q1) ? POTATO_GOOD : POTATO_NOT_GOOD;
  r.confidence = (r.predictedClass == POTATO_GOOD) ? p0 : p1;
#if ENABLE_CONFIDENCE_THRESHOLD
  if (r.confidence < CONFIDENCE_THRESHOLD) {
    r.belowThreshold = true;
    r.predictedClass = POTATO_UNKNOWN;
  }
#endif
  r.success = true;
  r.errorCode = CNN_OK;
  r.sequence = ++gSequence;
  return true;
}

bool classifyImage(const ImageView& image, int framing, ClassificationResult& r, PreprocStats* stats) {
  PreprocStats local;
  PreprocStats* st = stats ? stats : &local;
  const uint32_t t0 = micros();
  if (!preprocess(image, framing, st)) {
    r.success = false;
    r.predictedClass = POTATO_UNKNOWN;
    r.errorCode = CNN_ERR_PREPROCESS;
    return false;
  }
  r.preprocessTimeUs = st->elapsedUs;
  r.preprocessTimeMs = (st->elapsedUs + 500) / 1000;
  r.inputCrc32 = st->tensorCrc32;
  const bool ok = run(r);
  r.totalTimeUs = micros() - t0;
  r.totalTimeMs = (r.totalTimeUs + 500) / 1000;
  return ok;
}

void injectFaultForTest(TestFault fault) { gFault = fault; }

void printSummary() {
  Serial.printf("[MODEL] Ready=%s, arena %u/%u B in %s, model CRC32 %08lx\n", gReady ? "yes" : "no",
                (unsigned)arenaUsedBytes(), (unsigned)gArenaSize, gArenaPsram ? "PSRAM" : "internal RAM",
                (unsigned long)gModelCrc);
  if (gReady) {
    Serial.printf("[MODEL] Input  INT8 [1,%d,%d,%d] scale=%.8f zp=%ld\n", POTATO_INPUT_HEIGHT, POTATO_INPUT_WIDTH,
                  POTATO_INPUT_CHANNELS, gInput->params.scale, (long)gInput->params.zero_point);
    Serial.printf("[MODEL] Output INT8 [1,%d] scale=%.8f zp=%ld\n", POTATO_NUM_CLASSES, gOutput->params.scale,
                  (long)gOutput->params.zero_point);
  }
}

const char* errorString(CnnError e) {
  switch (e) {
    case CNN_OK: return "OK";
    case CNN_ERR_NOT_INITIALIZED: return "TFLM not initialized";
    case CNN_ERR_MODEL_MISALIGNED: return "model data misaligned";
    case CNN_ERR_MODEL_SIZE: return "model size mismatch";
    case CNN_ERR_MODEL_CRC: return "model CRC mismatch";
    case CNN_ERR_MODEL_IDENTIFIER: return "not a TFLite FlatBuffer";
    case CNN_ERR_MODEL_VERIFY: return "FlatBuffer verification failed";
    case CNN_ERR_SCHEMA_VERSION: return "schema version mismatch";
    case CNN_ERR_SUBGRAPH: return "unexpected subgraph count";
    case CNN_ERR_RESOLVER: return "op resolver registration failed";
    case CNN_ERR_UNSUPPORTED_OP: return "unsupported operator";
    case CNN_ERR_ARENA_ALLOC: return "tensor arena allocation failed";
    case CNN_ERR_INTERPRETER: return "interpreter construction failed";
    case CNN_ERR_ALLOCATE_TENSORS: return "AllocateTensors failed";
    case CNN_ERR_INPUT_TENSOR: return "invalid input tensor";
    case CNN_ERR_OUTPUT_TENSOR: return "invalid output tensor";
    case CNN_ERR_PREPROCESS: return "preprocessing failed";
    case CNN_ERR_INVOKE: return "Invoke failed";
    case CNN_ERR_OUTPUT_INVALID: return "invalid output values";
  }
  return "unknown";
}

}  // namespace cnn
