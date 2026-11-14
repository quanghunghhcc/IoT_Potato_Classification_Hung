// cnn_inference.h — Chạy CNN INT8 (potato_int8.tflite) bằng TensorFlow Lite Micro tích hợp sẵn trong
// Arduino-ESP32 Core 3.x (thư viện tiền biên dịch esp-tflite-micro + kernel tối ưu ESP-NN).
//
// Header này KHÔNG include header TensorFlow → .ino và các module khác không bị ảnh hưởng macro TFLM.
#pragma once

#include <Arduino.h>

#include "image_preprocessing.h"

// Chỉ số lớp phải khớp train.py: CLASSES = ("good", "not_good") và potato_metadata.json.
enum PotatoClass : uint8_t {
  POTATO_GOOD = 0,
  POTATO_NOT_GOOD = 1,
  POTATO_UNKNOWN = 2,  // chỉ dùng khi bật ENABLE_CONFIDENCE_THRESHOLD hoặc khi lỗi
};

enum CnnError {
  CNN_OK = 0,
  CNN_ERR_NOT_INITIALIZED,
  CNN_ERR_MODEL_MISALIGNED,
  CNN_ERR_MODEL_SIZE,
  CNN_ERR_MODEL_CRC,
  CNN_ERR_MODEL_IDENTIFIER,
  CNN_ERR_MODEL_VERIFY,
  CNN_ERR_SCHEMA_VERSION,
  CNN_ERR_SUBGRAPH,
  CNN_ERR_RESOLVER,
  CNN_ERR_UNSUPPORTED_OP,
  CNN_ERR_ARENA_ALLOC,
  CNN_ERR_INTERPRETER,
  CNN_ERR_ALLOCATE_TENSORS,
  CNN_ERR_INPUT_TENSOR,
  CNN_ERR_OUTPUT_TENSOR,
  CNN_ERR_PREPROCESS,
  CNN_ERR_INVOKE,
  CNN_ERR_OUTPUT_INVALID,
};

struct ClassificationResult {
  bool success;                 // false = lỗi hệ thống; KHÔNG được hiểu là NOT_GOOD
  PotatoClass predictedClass;
  float goodProbability;        // (q - zero_point) * scale
  float notGoodProbability;
  float confidence;             // xác suất của lớp được chọn
  int8_t rawGood;               // giá trị int8 thô của output tensor
  int8_t rawNotGood;
  bool tie;                     // rawGood == rawNotGood → argmax chọn index 0 (giống np.argmax)
  bool belowThreshold;          // chỉ khi ENABLE_CONFIDENCE_THRESHOLD = 1

  unsigned long captureTimeMs;
  unsigned long preprocessTimeMs;
  unsigned long inferenceTimeMs;
  unsigned long totalTimeMs;
  uint32_t captureTimeUs;
  uint32_t preprocessTimeUs;
  uint32_t inferenceTimeUs;
  uint32_t totalTimeUs;

  uint32_t inputCrc32;          // CRC32 của tensor INT8 đầu vào (đối chiếu Python)
  uint32_t sequence;            // số thứ tự lần phân loại
  int errorCode;                // CnnError hoặc mã lỗi hệ thống
};

void resetResult(ClassificationResult& r);
const char* potatoClassName(PotatoClass c);

namespace cnn {

bool begin();                   // kiểm tra model, đăng ký op, cấp phát arena, AllocateTensors()
bool isReady();
CnnError lastError();
const char* errorString(CnnError e);

// Truy cập input tensor (ghi trực tiếp, không sao chép thêm)
int8_t* inputData();
size_t inputBytes();
int inputWidth();
int inputHeight();
int inputChannels();
float inputScale();
int32_t inputZeroPoint();
float outputScale();
int32_t outputZeroPoint();

// Tiền xử lý ảnh trực tiếp vào input tensor.
bool preprocess(const ImageView& image, int framing, PreprocStats* stats);
// Invoke() + giải lượng tử + argmax. Ghi các trường kết quả CNN vào r (giữ nguyên các trường thời gian khác).
bool run(ClassificationResult& r);
// Tiện ích: preprocess + run trong một lần gọi (dùng cho kiểm thử).
bool classifyImage(const ImageView& image, int framing, ClassificationResult& r, PreprocStats* stats);

size_t arenaSize();
size_t arenaUsedBytes();
bool arenaInPsram();
void printSummary();

// Chèn lỗi một lần cho kiểm thử (TC09/TC13), đi qua đúng nhánh xử lý lỗi thật của run():
//   FAULT_INVOKE = coi như Invoke() trả lỗi; FAULT_OUTPUT = output [127,127] (tổng xác suất ≈ 2 → không hợp lệ)
enum TestFault { FAULT_NONE = 0, FAULT_INVOKE = 1, FAULT_OUTPUT = 2 };
void injectFaultForTest(TestFault fault);

}  // namespace cnn
