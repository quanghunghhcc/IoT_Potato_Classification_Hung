// image_preprocessing.cpp — xem image_preprocessing.h
//
// Tắt "floating-point contraction": nếu không, GCC có thể gộp a + b*c thành lệnh MADD.S của FPU
// ESP32-S3 (làm tròn 1 lần thay vì 2), khiến kết quả lệch với NumPy/TensorFlow ở các giá trị
// sát ngưỡng .5. Pragma phải đứng trước mọi định nghĩa hàm trong file.
#pragma GCC optimize("fp-contract=off")

#include "image_preprocessing.h"

#include <Arduino.h>
#include <math.h>

#include "config.h"
#include "diagnostics.h"

namespace {

constexpr int kMaxDstSize = 256;

struct Axis {
  int lo;
  int hi;
  float lerp;
};

// Bảng trọng số dùng lại giữa các lần gọi (không cấp phát động, không nằm trên stack).
Axis gAxisX[kMaxDstSize];
Axis gAxisY[kMaxDstSize];

struct Source {
  const uint8_t* data;
  int width;
  int height;
  PixelLayout layout;
  int addX;  // toạ độ khung hình = toạ độ ảnh ảo + add
  int addY;
  float pad[3];
};

inline void decodePixel(const uint8_t* data, PixelLayout layout, size_t index, uint8_t rgb[3]) {
  switch (layout) {
    case PIXEL_RGB565_BE: {
      const uint8_t* p = data + index * 2;
      const uint16_t v = static_cast<uint16_t>((p[0] << 8) | p[1]);  // byte cao trước
      const uint8_t r5 = (v >> 11) & 0x1F;
      const uint8_t g6 = (v >> 5) & 0x3F;
      const uint8_t b5 = v & 0x1F;
      rgb[0] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
      rgb[1] = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
      rgb[2] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
      break;
    }
    case PIXEL_RGB888: {
      const uint8_t* p = data + index * 3;
      rgb[0] = p[0];
      rgb[1] = p[1];
      rgb[2] = p[2];
      break;
    }
    case PIXEL_BGR888:
    default: {
      const uint8_t* p = data + index * 3;
      rgb[0] = p[2];
      rgb[1] = p[1];
      rgb[2] = p[0];
      break;
    }
  }
}

inline void fetch(const Source& s, int vx, int vy, float out[3]) {
  const int fx = vx + s.addX;
  const int fy = vy + s.addY;
  if (fx < 0 || fy < 0 || fx >= s.width || fy >= s.height) {  // vùng đệm của LETTERBOX
    out[0] = s.pad[0];
    out[1] = s.pad[1];
    out[2] = s.pad[2];
    return;
  }
  uint8_t rgb[3];
  decodePixel(s.data, s.layout, static_cast<size_t>(fy) * static_cast<size_t>(s.width) + fx, rgb);
  out[0] = static_cast<float>(rgb[0]);
  out[1] = static_cast<float>(rgb[1]);
  out[2] = static_cast<float>(rgb[2]);
}

// Trọng số bilinear half-pixel-centers (TensorFlow compute_interpolation_weights + HalfPixelScaler).
void computeAxis(int outSize, int inSize, Axis* axis) {
  const float scale = static_cast<float>(inSize) / static_cast<float>(outSize);
  for (int i = 0; i < outSize; ++i) {
    const float v = (static_cast<float>(i) + 0.5f) * scale - 0.5f;
    const float f = floorf(v);
    int lo = static_cast<int>(f);
    if (lo < 0) lo = 0;
    int hi = static_cast<int>(ceilf(v));
    if (hi > inSize - 1) hi = inSize - 1;
    if (hi < 0) hi = 0;
    axis[i].lo = lo;
    axis[i].hi = hi;
    axis[i].lerp = v - f;
  }
}

// Làm tròn về số nguyên gần nhất, trường hợp .5 về số chẵn (giống np.rint, không phụ thuộc libm).
inline int roundHalfEven(float x) {
  const float f = floorf(x);
  const float d = x - f;
  const int i = static_cast<int>(f);
  if (d > 0.5f) return i + 1;
  if (d < 0.5f) return i;
  return (i & 1) ? i + 1 : i;
}

// Màu đệm: trung bình 4 ô PATCH x PATCH lùi INSET pixel vào từ 4 góc (làm tròn nửa lên).
void estimatePadColor(const ImageView& img, uint8_t pad[3]) {
  const int p = PREPROC_PAD_PATCH;
  const int k = PREPROC_PAD_INSET;
  const int ys[2] = {k, img.height - k - p};
  const int xs[2] = {k, img.width - k - p};
  uint32_t acc[3] = {0, 0, 0};
  for (int yi = 0; yi < 2; ++yi) {
    for (int xi = 0; xi < 2; ++xi) {
      for (int y = ys[yi]; y < ys[yi] + p; ++y) {
        for (int x = xs[xi]; x < xs[xi] + p; ++x) {
          uint8_t rgb[3];
          decodePixel(img.data, img.layout, static_cast<size_t>(y) * img.width + x, rgb);
          acc[0] += rgb[0];
          acc[1] += rgb[1];
          acc[2] += rgb[2];
        }
      }
    }
  }
  const uint32_t n = 4u * p * p;
  for (int c = 0; c < 3; ++c) pad[c] = static_cast<uint8_t>((acc[c] + n / 2) / n);
}

}  // namespace

PreprocError preprocessToInt8(const ImageView& src, int framing, int8_t* dst, size_t dstBytes, int dstWidth,
                              int dstHeight, float inputScale, int32_t inputZeroPoint, PreprocStats* stats) {
  const uint32_t t0 = micros();
  if (src.data == nullptr || dst == nullptr) return PREPROC_ERR_NULL_POINTER;
  if (src.width < 2 * (PREPROC_PAD_INSET + PREPROC_PAD_PATCH) ||
      src.height < 2 * (PREPROC_PAD_INSET + PREPROC_PAD_PATCH) || dstWidth <= 0 || dstHeight <= 0 ||
      dstWidth > kMaxDstSize || dstHeight > kMaxDstSize) {
    return PREPROC_ERR_BAD_DIMENSIONS;
  }
  size_t bpp = 0;
  if (src.layout == PIXEL_RGB565_BE) bpp = 2;
  else if (src.layout == PIXEL_RGB888 || src.layout == PIXEL_BGR888) bpp = 3;
  else return PREPROC_ERR_UNSUPPORTED_LAYOUT;
  if (src.length < static_cast<size_t>(src.width) * src.height * bpp) return PREPROC_ERR_BUFFER_TOO_SMALL;
  if (dstBytes < static_cast<size_t>(dstWidth) * dstHeight * 3) return PREPROC_ERR_BUFFER_TOO_SMALL;
  if (!(inputScale > 0.0f) || !isfinite(inputScale)) return PREPROC_ERR_BAD_QUANT_PARAMS;

  Source s;
  s.data = src.data;
  s.width = src.width;
  s.height = src.height;
  s.layout = src.layout;
  s.pad[0] = s.pad[1] = s.pad[2] = 0.0f;
  uint8_t pad[3] = {0, 0, 0};
  int vw = src.width;
  int vh = src.height;

  switch (framing) {
    case FRAMING_STRETCH:
      s.addX = 0;
      s.addY = 0;
      break;
    case FRAMING_CENTER_CROP: {
      const int side = src.width < src.height ? src.width : src.height;
      vw = vh = side;
      s.addX = (src.width - side) / 2;
      s.addY = (src.height - side) / 2;
      break;
    }
    case FRAMING_LETTERBOX: {
      const int side = src.width > src.height ? src.width : src.height;
      vw = vh = side;
      s.addX = -((side - src.width) / 2);
      s.addY = -((side - src.height) / 2);
      estimatePadColor(src, pad);
      s.pad[0] = static_cast<float>(pad[0]);
      s.pad[1] = static_cast<float>(pad[1]);
      s.pad[2] = static_cast<float>(pad[2]);
      break;
    }
    default:
      return PREPROC_ERR_BAD_FRAMING;
  }

  computeAxis(dstWidth, vw, gAxisX);
  computeAxis(dstHeight, vh, gAxisY);
  const float zp = static_cast<float>(inputZeroPoint);

  for (int oy = 0; oy < dstHeight; ++oy) {
    const Axis& ay = gAxisY[oy];
    int8_t* row = dst + static_cast<size_t>(oy) * dstWidth * 3;
    for (int ox = 0; ox < dstWidth; ++ox) {
      const Axis& ax = gAxisX[ox];
      float tl[3], tr[3], bl[3], br[3];
      fetch(s, ax.lo, ay.lo, tl);
      fetch(s, ax.hi, ay.lo, tr);
      fetch(s, ax.lo, ay.hi, bl);
      fetch(s, ax.hi, ay.hi, br);
      for (int c = 0; c < 3; ++c) {
        // Thứ tự phép tính giống TensorFlow compute_lerp():
        const float top = tl[c] + (tr[c] - tl[c]) * ax.lerp;
        const float bottom = bl[c] + (br[c] - bl[c]) * ax.lerp;
        const float value = top + (bottom - top) * ay.lerp;  // pixel 0..255 (float)
        int q = roundHalfEven(value / inputScale + zp);
        if (q < -128) q = -128;
        if (q > 127) q = 127;
        row[ox * 3 + c] = static_cast<int8_t>(q);
      }
    }
  }

  if (stats != nullptr) {
    stats->framing = framing;
    stats->sourceWidth = src.width;
    stats->sourceHeight = src.height;
    stats->virtualWidth = vw;
    stats->virtualHeight = vh;
    stats->padColor[0] = pad[0];
    stats->padColor[1] = pad[1];
    stats->padColor[2] = pad[2];
    stats->tensorCrc32 = crc32Update(0, reinterpret_cast<const uint8_t*>(dst),
                                     static_cast<size_t>(dstWidth) * dstHeight * 3);
    stats->elapsedUs = micros() - t0;
  }
  return PREPROC_OK;
}

const char* preprocErrorString(PreprocError e) {
  switch (e) {
    case PREPROC_OK: return "OK";
    case PREPROC_ERR_NULL_POINTER: return "null pointer";
    case PREPROC_ERR_BAD_DIMENSIONS: return "invalid image/tensor dimensions";
    case PREPROC_ERR_BUFFER_TOO_SMALL: return "buffer too small for the declared size";
    case PREPROC_ERR_UNSUPPORTED_LAYOUT: return "unsupported pixel layout";
    case PREPROC_ERR_BAD_FRAMING: return "invalid PREPROC_FRAMING";
    case PREPROC_ERR_BAD_QUANT_PARAMS: return "invalid input quantization scale";
  }
  return "unknown";
}

const char* framingName(int framing) {
  switch (framing) {
    case FRAMING_STRETCH: return "STRETCH";
    case FRAMING_LETTERBOX: return "LETTERBOX";
    case FRAMING_CENTER_CROP: return "CENTER_CROP";
  }
  return "INVALID";
}

const char* pixelLayoutName(PixelLayout layout) {
  switch (layout) {
    case PIXEL_RGB565_BE: return "RGB565 (big-endian)";
    case PIXEL_RGB888: return "RGB888";
    case PIXEL_BGR888: return "BGR888";
  }
  return "INVALID";
}
