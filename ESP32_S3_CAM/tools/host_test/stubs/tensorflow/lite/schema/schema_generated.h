#pragma once
#include "flatbuffers/flatbuffers.h"
namespace tflite {
enum BuiltinOperator : int32_t { BuiltinOperator_ADD = 0, BuiltinOperator_CONV_2D = 3, BuiltinOperator_DEPTHWISE_CONV_2D = 4,
  BuiltinOperator_FULLY_CONNECTED = 9, BuiltinOperator_MAX_POOL_2D = 17, BuiltinOperator_SOFTMAX = 25, BuiltinOperator_CUSTOM = 32, BuiltinOperator_MEAN = 40 };
const char* EnumNameBuiltinOperator(BuiltinOperator e);
struct OperatorCode { int8_t deprecated_builtin_code() const; const flatbuffers::String* custom_code() const; int32_t version() const; BuiltinOperator builtin_code() const; };
struct SubGraph {};
struct Model { uint32_t version() const; const flatbuffers::Vector<flatbuffers::Offset<OperatorCode>>* operator_codes() const;
  const flatbuffers::Vector<flatbuffers::Offset<SubGraph>>* subgraphs() const; };
const Model* GetModel(const void* buf);
bool VerifyModelBuffer(flatbuffers::Verifier& verifier);
}
