// 05_Test_CNN — Kiểm tra TFLM + model INT8 + tiền xử lý C++ KHÔNG cần camera, cảm biến hay LED.
//
// Dữ liệu kiểm thử (test_vectors.cpp) sinh bởi tools/export_test_vectors.py từ ảnh THẬT trong dataset/test:
//   Phần A: tensor INT8 tạo bằng đúng pipeline huấn luyện (tf.image.resize + np.rint) → so sánh output
//           int8 của TFLM trên ESP32 với TFLite (kernel tham chiếu BUILTIN_REF) trên PC.
//           Sai ở phần này = lỗi runtime/model (không liên quan camera).
//   Phần B: khung hình RGB565 320x240 mô phỏng camera → image_preprocessing.cpp trên ESP32 → so CRC32
//           của tensor với bản tham chiếu NumPy (tools/preprocess_reference.py) cho 3 chế độ framing.
//           Sai ở phần này = lỗi tiền xử lý (byte order, màu, resize, làm tròn).
//   Phần C: đo thời gian Invoke() (không hứa hẹn số liệu trước khi đo trên phần cứng).
// Gõ 'r' trong Serial Monitor để chạy lại.
#include <Arduino.h>
#include <string.h>

#include "cnn_inference.h"
#include "config.h"
#include "diagnostics.h"
#include "image_preprocessing.h"
#include "model_data.h"
#include "test_vectors.h"

// Sai lệch tối đa cho phép (đơn vị LSB int8 = 1/256 xác suất) giữa TFLM và TFLite BUILTIN_REF.
// Kernel tối ưu khác nhau có thể làm tròn khác nhau (trên PC, BUILTIN_REF và kernel tối ưu đã lệch tới
// 6 LSB với good_469.jpg). Nhãn dự đoán PHẢI trùng.
static const int kRawTolerance = 8;

static int maxAbsDiff(const int8_t a[2], const int8_t b[2]) {
  const int d0 = abs(static_cast<int>(a[0]) - b[0]);
  const int d1 = abs(static_cast<int>(a[1]) - b[1]);
  return d0 > d1 ? d0 : d1;
}

static int argmaxOf(const int8_t raw[2]) { return raw[0] >= raw[1] ? 0 : 1; }

static bool partA() {
  Serial.println();
  Serial.printf("=== Part A: %d real test tensors (training pipeline) -> TFLM vs TFLite ===\n", kCnnTestVectorCount);
  int pass = 0, exact = 0, truth = 0;
  for (int i = 0; i < kCnnTestVectorCount; ++i) {
    const CnnTestVector& v = kCnnTestVectors[i];
    if (cnn::inputBytes() != 96u * 96u * 3u) return false;
    memcpy(cnn::inputData(), v.tensor, cnn::inputBytes());
    const uint32_t crc = crc32Update(0, reinterpret_cast<const uint8_t*>(cnn::inputData()), cnn::inputBytes());
    ClassificationResult r;
    resetResult(r);
    const bool ok = cnn::run(r);
    const int8_t got[2] = {r.rawGood, r.rawNotGood};
    const int diff = ok ? maxAbsDiff(got, v.expectedRaw) : 999;
    const bool labelOk = ok && argmaxOf(got) == argmaxOf(v.expectedRaw);
    const bool p = ok && crc == v.crc32 && labelOk && diff <= kRawTolerance;
    pass += p;
    exact += (ok && diff == 0);
    truth += (ok && static_cast<int>(r.predictedClass) == v.label);
    Serial.printf("[%s] %-40s esp32=[%4d,%4d] ref=[%4d,%4d] opt=[%4d,%4d] diff=%d pred=%s truth=%s %.1f ms%s\n",
                  p ? "PASS" : "FAIL", v.name, got[0], got[1], v.expectedRaw[0], v.expectedRaw[1],
                  v.expectedRawOptimized[0], v.expectedRawOptimized[1], diff, potatoClassName(r.predictedClass),
                  v.label == 0 ? "good" : "not_good", r.inferenceTimeUs / 1000.0f,
                  crc == v.crc32 ? "" : "  (INPUT CRC MISMATCH)");
    Serial.printf("#CNN_VECTOR,%d,%d,%d,%d,%d\n", i, got[0], got[1], v.expectedRaw[0], v.expectedRaw[1]);
  }
  Serial.printf("Part A: %d/%d PASS, %d/%d bit-exact vs BUILTIN_REF, %d/%d match ground-truth label\n", pass,
                kCnnTestVectorCount, exact, kCnnTestVectorCount, truth, kCnnTestVectorCount);
  return pass == kCnnTestVectorCount;
}

static bool partB() {
  Serial.println();
  Serial.printf("=== Part B: C++ preprocessing of %s ===\n", kTestFrameSource);
  const ImageView frame = {kTestFrameRgb565, kTestFrameLen, kTestFrameWidth, kTestFrameHeight, PIXEL_RGB565_BE};
  int pass = 0;
  for (int i = 0; i < kFrameExpectationCount; ++i) {
    const FrameExpectation& e = kFrameExpectations[i];
    PreprocStats st = {};
    if (!cnn::preprocess(frame, e.framing, &st)) {
      Serial.printf("[FAIL] %s: preprocessing error\n", framingName(e.framing));
      continue;
    }
    int mismatches = 0, maxDiff = 0;
    if (e.framing == FRAMING_LETTERBOX) {
      const int8_t* t = cnn::inputData();
      for (size_t k = 0; k < cnn::inputBytes(); ++k) {
        const int d = abs(static_cast<int>(t[k]) - kFrameLetterboxTensor[k]);
        if (d) {
          if (mismatches < 5)
            Serial.printf("   mismatch at index %u (y=%u x=%u c=%u): esp32=%d python=%d\n", (unsigned)k,
                          (unsigned)(k / 288), (unsigned)((k / 3) % 96), (unsigned)(k % 3), t[k],
                          kFrameLetterboxTensor[k]);
          ++mismatches;
          if (d > maxDiff) maxDiff = d;
        }
      }
    }
    const bool crcOk = st.tensorCrc32 == e.crc32;
    ClassificationResult r;
    resetResult(r);
    const bool ok = cnn::run(r);
    const int8_t got[2] = {r.rawGood, r.rawNotGood};
    const int diff = ok ? maxAbsDiff(got, e.expectedRaw) : 999;
    const bool p = crcOk && ok && argmaxOf(got) == argmaxOf(e.expectedRaw) && diff <= kRawTolerance;
    pass += p;
    Serial.printf("[%s] %-11s tensor CRC32 esp32=%08lx python=%08lx%s  pad=(%u,%u,%u) %.1f ms  output esp32=[%d,%d] "
                  "ref=[%d,%d] -> %s\n",
                  p ? "PASS" : "FAIL", framingName(e.framing), (unsigned long)st.tensorCrc32, (unsigned long)e.crc32,
                  crcOk ? " (identical)" : " (DIFFERENT)", st.padColor[0], st.padColor[1], st.padColor[2],
                  st.elapsedUs / 1000.0f, got[0], got[1], e.expectedRaw[0], e.expectedRaw[1],
                  potatoClassName(r.predictedClass));
    if (e.framing == FRAMING_LETTERBOX)
      Serial.printf("           LETTERBOX element mismatches: %d (max |diff| %d)\n", mismatches, maxDiff);
  }
  Serial.printf("Part B: %d/%d PASS\n", pass, kFrameExpectationCount);
  return pass == kFrameExpectationCount;
}

static void partC() {
  Serial.println();
  Serial.println("=== Part C: Invoke() timing (20 runs, same input) ===");
  memcpy(cnn::inputData(), kCnnTestVectors[0].tensor, cnn::inputBytes());
  uint32_t mn = UINT32_MAX, mx = 0;
  uint64_t sum = 0;
  for (int i = 0; i < 20; ++i) {
    ClassificationResult r;
    resetResult(r);
    if (!cnn::run(r)) {
      Serial.println("[FAIL] Invoke failed");
      return;
    }
    mn = r.inferenceTimeUs < mn ? r.inferenceTimeUs : mn;
    mx = r.inferenceTimeUs > mx ? r.inferenceTimeUs : mx;
    sum += r.inferenceTimeUs;
  }
  Serial.printf("Invoke: min %.2f ms, avg %.2f ms, max %.2f ms (arena in %s, CPU %lu MHz)\n", mn / 1000.0f,
                (double)sum / 20.0 / 1000.0, mx / 1000.0f, cnn::arenaInPsram() ? "PSRAM" : "internal RAM",
                (unsigned long)ESP.getCpuFreqMHz());
}

static void runAll() {
  const bool a = partA();
  const bool b = partB();
  partC();
  printMemoryInfo("MEMORY");
  Serial.println();
  Serial.printf("RESULT: Part A (model/runtime) %s, Part B (preprocessing) %s\n", a ? "PASS" : "FAIL",
                b ? "PASS" : "FAIL");
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < SERIAL_WAIT_MS) delay(10);
  delay(200);
  Serial.println();
  Serial.println("===== 05_Test_CNN =====");
  if (!psramFound()) {
    Serial.println("[FAIL] PSRAM not found -> Tools > PSRAM > OPI PSRAM");
    return;
  }
  if (!cnn::begin()) {
    Serial.printf("[FAIL] TFLM init: %s\n", cnn::errorString(cnn::lastError()));
    return;
  }
  runAll();
}

void loop() {
  if (Serial.available() > 0 && Serial.read() == 'r' && cnn::isReady()) runAll();
  delay(10);
}
