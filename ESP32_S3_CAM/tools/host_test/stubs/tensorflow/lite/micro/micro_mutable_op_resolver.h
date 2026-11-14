#pragma once
#include "tensorflow/lite/micro/micro_interpreter.h"
namespace tflite {
template <unsigned int tOpCount> class MicroMutableOpResolver : public MicroOpResolver { public:
  const TFLMRegistration* FindOp(BuiltinOperator op) const override; const TFLMRegistration* FindOp(const char* op) const override;
  TfLiteStatus AddConv2D(); TfLiteStatus AddDepthwiseConv2D(); TfLiteStatus AddMaxPool2D(); TfLiteStatus AddMean();
  TfLiteStatus AddFullyConnected(); TfLiteStatus AddSoftmax(); };
}
