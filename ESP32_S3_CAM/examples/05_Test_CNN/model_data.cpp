// model_data.cpp — Translation unit DUY NHẤT include potato_model_data.h.
//
// potato_model_data.h là file GỐC do train.py sinh ra (sao chép nguyên văn từ models/ bởi
// tools/sync_project.py, byte model giống hệt potato_int8.tflite). Nó định nghĩa:
//     alignas(16) const unsigned char g_potato_model[] = {...};
//     const unsigned int g_potato_model_len = sizeof(g_potato_model);
// Ở C++, biến const phạm vi namespace mặc định có liên kết nội bộ. model_data.h khai báo extern
// TRƯỚC, nên hai định nghĩa trên có liên kết ngoài: chỉ một bản model trong flash (.rodata, đọc
// trực tiếp qua cache, không copy vào RAM/PSRAM), các file khác truy cập qua model_data.h.
#include "model_data.h"

#include "potato_model_data.h"

static_assert(sizeof(g_potato_model) == POTATO_MODEL_EXPECTED_LEN,
              "potato_model_data.h khong khop model_data.h: chay tools/sync_project.py");
// Căn lề 16 byte (yêu cầu của TFLM/FlatBuffers) được kiểm tra lúc chạy trong cnn::begin().
