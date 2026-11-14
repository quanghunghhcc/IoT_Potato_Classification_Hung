# MODEL_INFO — Mô hình CNN INT8 phân loại khoai tây

Mọi số liệu dưới đây được đo trực tiếp từ các file trong `models/` bằng các script trong `tools/`
(TensorFlow 2.21 trong `.venv`). Không có số liệu nào được tự đặt ra. Báo cáo gốc dạng JSON:
`tools/model_report.json`, `tools/framing_report.json`, `tools/test_vectors_report.json`.

## 1. File mô hình

| File | Vai trò | Kiểm tra |
|---|---|---|
| `models/potato_best.keras` | checkpoint Keras (chỉ dùng trên PC) | không dùng trên ESP32 |
| `models/potato_int8.tflite` | mô hình full-INT8 dùng trên ESP32 | 19 136 B, `TFL3`, schema v3 |
| `models/potato_model_data.h` | mảng C do `train.py` sinh | **giống hệt từng byte** với `.tflite` |
| `models/potato_metadata.json` | thứ tự lớp, lượng tử hóa, accuracy | khớp với tensor đọc từ `.tflite` |

- SHA-256: `279637e660d77f8fd4343857b7b3fe1568c7da61a67816d94ea60d304067eb3d`
- CRC32: `0x5dacc339` (firmware kiểm tra lại lúc khởi động, `MODEL_ENFORCE_CRC`)
- Trọng số (các buffer hằng): 5 960 B; 1 subgraph; 35 tensor.

## 2. Input / Output tensor

| | Shape | Kiểu | scale | zero_point |
|---|---|---|---|---|
| Input `serving_default_rgb888_0_to_255:0` | `[1, 96, 96, 3]` (NHWC, R-G-B) | INT8 | `1.0` | `-128` |
| Output `StatefulPartitionedCall_1:0` | `[1, 2]` | INT8 | `0.00390625` (1/256) | `-128` |

- Input nhận **pixel 0–255**; lớp `Rescaling(1/255)` nằm *bên trong* mô hình → firmware **không** chia 255.
- Với scale = 1.0, zero_point = −128: `q = clamp(round(x/1.0 − 128))`. Vì sau resize bilinear `x` là số thực,
  làm tròn vẫn có ý nghĩa: firmware dùng **round-half-to-even** giống hệt `np.rint` trong `train.py`.
  Firmware **không hardcode** `pixel − 128`: scale/zero_point được đọc từ tensor lúc chạy và chỉ đối chiếu
  (cảnh báo) với `potato_metadata.json`.
- Output: `p = (q + 128) / 256`. Ví dụ `[127, -128]` → `p_good = 0.9961`, `p_not_good = 0.0`.

## 3. Thứ tự lớp

`train.py`: `CLASSES = ("good", "not_good")` · `potato_metadata.json`: `["good", "not_good"]` → **index 0 = good,
index 1 = not_good**. `tools/sync_project.py` dừng với lỗi nếu hai nguồn này khác nhau.

Quy tắc dự đoán = `np.argmax` (như `train.py`): nếu hai giá trị int8 bằng nhau, chọn index 0 (good) và in
`TIE`. Ngưỡng confidence là tùy chọn, **tắt mặc định** (`ENABLE_CONFIDENCE_THRESHOLD 0`).

## 4. Toán tử (đọc từ FlatBuffer thật)

| # | Toán tử | Version | Output shape |
|---|---|---|---|
| 0 | CONV_2D | 3 | 1×96×96×16 |
| 1 | MAX_POOL_2D | 2 | 1×48×48×16 |
| 2 | DEPTHWISE_CONV_2D | 3 | 1×48×48×16 |
| 3 | CONV_2D | 3 | 1×48×48×24 |
| 4 | MAX_POOL_2D | 2 | 1×24×24×24 |
| 5 | DEPTHWISE_CONV_2D | 3 | 1×24×24×24 |
| 6 | CONV_2D | 3 | 1×24×24×32 |
| 7 | MAX_POOL_2D | 2 | 1×12×12×32 |
| 8 | DEPTHWISE_CONV_2D | 3 | 1×12×12×32 |
| 9 | CONV_2D | 3 | 1×12×12×48 |
| 10 | MAX_POOL_2D | 2 | 1×6×6×48 |
| 11 | MEAN | 2 | 1×48 |
| 12 | FULLY_CONNECTED | 4 | 1×24 |
| 13 | FULLY_CONNECTED | 4 | 1×2 |
| 14 | SOFTMAX | 2 | 1×2 |

6 loại toán tử, không có QUANTIZE/DEQUANTIZE (input/output đã là INT8), không có custom op.
`SeparableConv2D` được converter tách thành DEPTHWISE_CONV_2D + CONV_2D 1×1; `GlobalAveragePooling2D` → MEAN;
`Dropout` bị loại khi export. Firmware đăng ký **đúng 6 op** bằng `MicroMutableOpResolver<6>` và trước khi tạo
interpreter còn duyệt lại danh sách op trong model, in tên + version và báo `NOT SUPPORTED` nếu thiếu.

## 5. Thư viện TFLM và tính tương thích

| Hạng mục | Kết luận | Nguồn |
|---|---|---|
| Thư viện | `esp-tflite-micro` **tích hợp sẵn trong Arduino-ESP32 Core ≥ 3.0.0** (thư viện tĩnh tiền biên dịch, link mặc định) | maintainer Espressif trong arduino-esp32 issue #9416; wiki pschatzmann/tflite-micro "ESP32" |
| Phiên bản trong core 3.3.x | esp-tflite-micro 1.4.x, esp-nn 1.4.x (manifest build IDF v5.5.6 nhánh 3.3.x); wiki ghi 1.4.0 cho core 3.3.12 | arduino-esp32 PR #13000 |
| ESP-NN | core build dùng kernel tối ưu ESP-NN cho S3 | wiki pschatzmann/tflite-micro |
| ABI | cùng toolchain với core → không có rủi ro ABI của thư viện cộng đồng tiền biên dịch | — |
| `TF_LITE_STATIC_MEMORY` | esp-tflite-micro luôn build với cờ này; `cnn_inference.cpp` define dự phòng trước mọi header TFLM | CMakeLists.txt của esp-tflite-micro |
| Op của model | cả 6 op đều là op chuẩn của TFLM, hỗ trợ INT8 | — |

Các lựa chọn khác đã cân nhắc và **không** dùng: `TensorFlowLite_ESP32` (tác giả ghi "no longer recommended,
outdated"), `TFLiteMicro_ArduinoESP32S3` (bản tiền biên dịch, không ghi phiên bản core/compiler hỗ trợ — rủi ro ABI),
`Chirale_TensorFlowLite` (không nêu ESP32-S3), `tflite-micro` của pschatzmann (chỉ kernel tham chiếu, không ESP-NN).

> ⚠ Chưa biên dịch thử với toolchain ESP32 thật trong môi trường thực hiện (xem README §2). API được viết theo
> TFLM hiện hành: `tflite::GetModel`, `MicroMutableOpResolver<N>::AddConv2D()...`, `MicroInterpreter(model,
> resolver, arena, size)` (không còn tham số ErrorReporter), `AllocateTensors()`, `arena_used_bytes()`.

## 6. Tensor arena

- Phân tích vòng đời tensor (`tools/inspect_model.py`): đỉnh bộ nhớ activation **184 320 B** tại op 1
  (tensor 96×96×16 + 48×48×16 INT8). Đây là **cận dưới**; chưa tính dữ liệu persistent của interpreter và
  scratch buffer của kernel ESP-NN.
- Firmware cấp `TENSOR_ARENA_SIZE = 320 KB` trong **PSRAM** (`heap_caps_aligned_alloc`, căn 16 byte, cấp một
  lần, không bao giờ free), sau `AllocateTensors()` in `Tensor arena used: X / 327680 B`.
  **Giá trị X chỉ biết khi chạy trên board** — hãy ghi lại và có thể giảm arena = X + ~20%.
- `ARENA_LOCATION`: `ARENA_IN_PSRAM` (mặc định, an toàn) / `ARENA_IN_INTERNAL` / `ARENA_AUTO` (RAM nội nếu còn
  dư ≥ 64 KB). RAM nội thường nhanh hơn PSRAM — chỉ đổi sau khi đo bằng `05_Test_CNN` Part C.

## 7. Cách nhúng mô hình

```
models/potato_model_data.h ──(tools/sync_project.py, sao chép NGUYÊN VĂN)──► Potato_Classifier_ESP32S3/potato_model_data.h
                                                       └─(sinh)──► model_data.h  (extern + size/CRC/lượng tử hóa)
model_data.cpp:  #include "model_data.h"        // khai báo extern trước → liên kết ngoài
                 #include "potato_model_data.h" // định nghĩa mảng — chỉ trong translation unit này
```

- Mảng `alignas(16) const` nằm trong flash (`.rodata`), TFLM đọc trực tiếp qua cache: **một bản duy nhất**,
  không copy vào RAM/PSRAM, không nằm trên stack. Firmware in vị trí (`flash, memory-mapped`) và kiểm tra căn lề.
- `static_assert(sizeof(g_potato_model) == POTATO_MODEL_EXPECTED_LEN)` bắt lỗi header không khớp lúc biên dịch.
- Lúc chạy: kiểm tra kích thước, CRC32, định danh `TFL3`, `flatbuffers::Verifier`, schema version, số subgraph.

**Khi huấn luyện lại:** chạy `train.py` → `tools/sync_project.py` → `tools/export_test_vectors.py` → nạp lại.

## 8. Dữ liệu huấn luyện — phát hiện quan trọng (ảnh hưởng độ tin cậy ngoài thực tế)

Phân tích `dataset/` (không sửa mô hình):

| Tập | good | not_good |
|---|---|---|
| Kích thước ảnh | 224×224 (~51%), 512×512 (~44%), ảnh gốc 4160×3120 / 3120×4160 (~5%) | **100% 512×512** |
| Nền | chủ yếu nền trắng, ảnh vuông | nền bê tông xám, **dải pad xám trên/dưới** (ảnh 4:3 được letterbox) |

→ Nhãn có tương quan mạnh với **nguồn ảnh** (nền, dải pad, kích thước gốc). Accuracy 99,76% trên tập test
(cùng phân phối) **không đảm bảo** mô hình nhận ra khuyết tật trên ảnh camera thật với nền/ánh sáng khác.

Thăm dò thêm (`tools/framing_study.py`): ảnh một màu (trắng 240, xám 200, xám bê tông, đen) và mảng **nền
không có khoai tây** cắt từ ảnh *good* đều bị dự đoán **not_good** (p_not_good 0,78–0,99; 91% mảng nền ảnh good).
Mô hình **không có lớp "không có vật"**: chụp hụt (khoai tây ngoài khung) gần như chắc chắn ra NOT_GOOD.

## 9. Chọn cách đóng khung ảnh (framing)

Khung hình QVGA 320×240 (4:3) được mô phỏng từ **421 ảnh test thật** (cắt 4:3 → resize 320×240 → RGB565),
qua đúng thuật toán của firmware, rồi chạy `potato_int8.tflite`:

| Framing | Accuracy good (277) | Accuracy not_good (144) | Tổng |
|---|---|---|---|
| Pipeline huấn luyện gốc (tham chiếu) | 99,64% | 100% | 99,76% |
| **LETTERBOX** (mặc định) | 99,64% | 100% | **99,76%** |
| STRETCH | 99,64% | 97,22% | 98,81% |
| CENTER_CROP | 100% | 90,28% | 96,67% |

LETTERBOX (đệm 320×240 thành 320×320 bằng màu trung bình 4 góc, rồi resize) tái tạo đúng hình học của ảnh
512×512 trong dataset nên được chọn mặc định (`PREPROC_FRAMING`). Đây là mô phỏng từ chính dataset; ảnh camera
thật có thể khác — hãy kiểm chứng bằng `tools/serial_compare.py`.

## 10. Tương đương tiền xử lý & runtime (đã kiểm tra trên PC)

| Kiểm tra | Kết quả |
|---|---|
| Bilinear NumPy (`preprocess_reference.py`) vs `tf.image.resize` trên 43 ảnh test thật | sai lệch float 0, int8 0 |
| **`image_preprocessing.cpp` thật** (g++ trên PC) vs NumPy: 88 khung hình × 3 framing (60 khung từ ảnh test thật, ảnh nhiễu 5 kích thước kể cả dọc, ảnh đơn sắc, layout RGB565/RGB888/BGR888) | **0 / 7 299 072 phần tử khác** |
| Cùng phép thử, biên dịch với FMA (`-mfma -ffp-contract=fast`) | 0 khác (nhờ `#pragma fp-contract=off`); bỏ pragma → 11 phần tử lệch ±1 |
| `crc32Update()` C++ vs `zlib.crc32` | khớp 8/8 |
| TFLite kernel tham chiếu vs kernel tối ưu trên PC, 8 ảnh | 7/8 giống hệt; `good_469.jpg`: `[28,-28]` vs `[22,-22]` (cùng nhãn) |

Hàng cuối cho thấy hai runtime chính thức đã có thể lệch vài LSB; vì vậy `05_Test_CNN` so với kết quả **kernel
tham chiếu** (`BUILTIN_REF`), yêu cầu **cùng nhãn** và sai lệch ≤ 8 LSB (≈3% xác suất), đồng thời báo số ca
bit-exact. Kết quả TFLM trên ESP32 **chưa được đo** (cần phần cứng).
