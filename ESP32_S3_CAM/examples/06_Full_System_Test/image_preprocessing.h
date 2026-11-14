// image_preprocessing.h — Ảnh camera → tensor INT8 [1, H, W, 3] (NHWC, thứ tự kênh R, G, B)
//
// Pipeline (phải khớp tools/preprocess_reference.py, đã đối chiếu bit-exact với tf.image.resize):
//   1. Giải mã pixel: RGB565 big-endian (esp32-camera) → RGB888 bằng bit replication
//        r8 = r5<<3 | r5>>2,  g8 = g6<<2 | g6>>4,  b8 = b5<<3 | b5>>2
//   2. Framing ảo (không tạo bản sao ảnh): STRETCH / LETTERBOX / CENTER_CROP
//   3. Resize bilinear half-pixel-centers — y hệt tf.image.resize(method="bilinear", antialias=False)
//      mà tf.keras.utils.image_dataset_from_directory(interpolation="bilinear") dùng khi huấn luyện.
//   4. Lượng tử hóa: q = clamp(round_half_even(x / scale + zero_point), -128, 127)
//      (round_half_even == np.rint trong train.py). scale/zero_point lấy từ input tensor lúc chạy.
//   KHÔNG chia 255: lớp Rescaling(1/255) đã nằm trong mô hình.
//
// Module này không phụ thuộc camera → có thể kiểm thử bằng dữ liệu nhúng (examples/05_Test_CNN).
#pragma once

#include <stddef.h>
#include <stdint.h>

enum PixelLayout : uint8_t {
  PIXEL_RGB565_BE = 0,  // 2 byte/pixel, byte cao trước (định dạng RGB565 của esp32-camera)
  PIXEL_RGB888 = 1,     // 3 byte/pixel R, G, B
  PIXEL_BGR888 = 2,     // 3 byte/pixel B, G, R (đầu ra fmt2rgb888() của esp32-camera)
};

struct ImageView {
  const uint8_t* data;
  size_t length;
  int width;
  int height;
  PixelLayout layout;
};

struct PreprocStats {
  int framing;
  int sourceWidth;
  int sourceHeight;
  int virtualWidth;   // kích thước ảnh ảo sau framing (vd. 320x320 với LETTERBOX)
  int virtualHeight;
  uint8_t padColor[3];  // màu đệm (chỉ LETTERBOX)
  uint32_t tensorCrc32;  // CRC32 của tensor INT8 đầu ra (so sánh với Python)
  uint32_t elapsedUs;
};

enum PreprocError {
  PREPROC_OK = 0,
  PREPROC_ERR_NULL_POINTER,
  PREPROC_ERR_BAD_DIMENSIONS,
  PREPROC_ERR_BUFFER_TOO_SMALL,
  PREPROC_ERR_UNSUPPORTED_LAYOUT,
  PREPROC_ERR_BAD_FRAMING,
  PREPROC_ERR_BAD_QUANT_PARAMS,
};

// dst phải có ít nhất dstWidth * dstHeight * 3 byte.
PreprocError preprocessToInt8(const ImageView& src, int framing, int8_t* dst, size_t dstBytes, int dstWidth,
                              int dstHeight, float inputScale, int32_t inputZeroPoint, PreprocStats* stats);

const char* preprocErrorString(PreprocError e);
const char* framingName(int framing);
const char* pixelLayoutName(PixelLayout layout);
