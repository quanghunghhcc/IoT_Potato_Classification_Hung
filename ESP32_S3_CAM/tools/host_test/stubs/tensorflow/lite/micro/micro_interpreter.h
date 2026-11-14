#pragma once
#include <cstddef>
#include <cstdint>
#include "tensorflow/lite/schema/schema_generated.h"
#ifndef TF_LITE_STATIC_MEMORY
#error "stub: expected TF_LITE_STATIC_MEMORY before TFLM headers"
#endif
typedef enum TfLiteStatus { kTfLiteOk = 0, kTfLiteError = 1 } TfLiteStatus;
typedef enum { kTfLiteNoType = 0, kTfLiteFloat32 = 1, kTfLiteInt32 = 2, kTfLiteUInt8 = 3, kTfLiteInt8 = 9 } TfLiteType;
const char* TfLiteTypeGetName(TfLiteType type);
typedef struct TfLiteIntArray { int size; int data[]; } TfLiteIntArray;
typedef struct TfLiteQuantizationParams { float scale; int32_t zero_point; } TfLiteQuantizationParams;
typedef union TfLitePtrUnion { int32_t* i32; float* f; int8_t* int8; uint8_t* uint8; void* data; } TfLitePtrUnion;
typedef struct TfLiteTensor { TfLiteType type; TfLitePtrUnion data; TfLiteIntArray* dims; TfLiteQuantizationParams params; size_t bytes; } TfLiteTensor;
namespace tflite {
struct TFLMRegistration { void* init; };
class MicroResourceVariables;
class MicroProfilerInterface;
class MicroOpResolver { public: virtual ~MicroOpResolver() {}
  virtual const TFLMRegistration* FindOp(BuiltinOperator op) const = 0; virtual const TFLMRegistration* FindOp(const char* op) const = 0; };
class MicroInterpreter { public:
  MicroInterpreter(const Model* model, const MicroOpResolver& op_resolver, uint8_t* tensor_arena, size_t tensor_arena_size,
                   MicroResourceVariables* resource_variables = nullptr, MicroProfilerInterface* profiler = nullptr, bool preserve_all_tensors = false);
  ~MicroInterpreter();
  TfLiteStatus AllocateTensors(); TfLiteStatus Invoke();
  TfLiteTensor* input(size_t index); TfLiteTensor* output(size_t index);
  size_t inputs_size() const; size_t outputs_size() const;
  TfLiteStatus initialization_status() const; size_t arena_used_bytes() const; };
}
