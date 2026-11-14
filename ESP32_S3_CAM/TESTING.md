# TESTING — Kế hoạch và kết quả kiểm thử

Trạng thái được ghi trung thực. **Chưa có test case phần cứng nào được chạy** (không có board trong môi trường
thực hiện). Cột "Trạng thái" phải được cập nhật bằng bằng chứng log thật — không đánh dấu PASS khi chưa chạy.

## 1. Thứ tự kiểm thử khuyến nghị

| Bước | Sketch | Mục đích | Tiêu chí đạt |
|---|---|---|---|
| 1 | `examples/01_Test_PSRAM` | PSRAM 8 MB OPI hoạt động | `RESULT: PASS` |
| 2 | `examples/04_Test_LEDs` | đấu LED, cực tính | xanh/đỏ sáng đúng lúc, không bao giờ sáng cùng (trừ lúc nháy lỗi) |
| 3 | `examples/02_Test_Sensor` | cực tính, debounce, điện áp OUT | `Object detected` / `Object removed` đúng thực tế; xung ngắn → `glitch` |
| 4 | `examples/03_Test_Camera` | pinout camera, OV3660, màu, hướng ảnh | `PID 0x3660`; lệnh `d` → PNG đúng màu/hướng |
| 5 | `examples/05_Test_CNN` | TFLM + model + tiền xử lý C++ (không cần camera) | `Part A PASS`, `Part B PASS` |
| 6 | `examples/06_Full_System_Test` | toàn hệ thống + báo cáo chống lặp/bộ nhớ | các TC ở §3 |
| 7 | `Potato_Classifier_ESP32S3` | firmware vận hành | như 6 |

Trước khi nạp các sketch, chạy `tools/sync_project.py --check` (phải báo `0 out of sync`).

## 2. Kiểm tra tương đương Python ↔ ESP32 (phân biệt lỗi model với lỗi camera/tiền xử lý)

```
           ┌──────────── 05_Test_CNN Part A ────────────┐
ảnh test → pipeline huấn luyện (PC) → tensor INT8 ──► TFLM (ESP32)   so với   TFLite BUILTIN_REF (PC)
                                                     sai ⇒ lỗi RUNTIME/MODEL

           ┌──────────── 05_Test_CNN Part B ────────────┐
khung RGB565 mô phỏng → image_preprocessing.cpp (ESP32) → CRC32   so với   preprocess_reference.py (PC)
                                                     sai ⇒ lỗi TIỀN XỬ LÝ

           ┌──────────── camera thật: tools/serial_compare.py ────────────┐
firmware: 't' + 'f' + 'm' → BEGIN_FRAME + BEGIN_TENSOR + #RESULT qua Serial
PC:  FRAME → frame_<seq>.png + tensor tham chiếu ─┬─ so với TENSOR của ESP32 ⇒ tiền xử lý trên ảnh thật
     TENSOR → tensor_<seq>.png + TFLite(PC) ──────┴─ so với #RESULT của ESP32 ⇒ runtime trên ảnh thật
     cả hai khớp nhưng nhãn sai ⇒ vấn đề ẢNH (ánh sáng, nền, khoảng cách, framing) hoặc giới hạn mô hình
```

Các lỗi được nhắm tới: RGB/BGR đảo, byte order RGB565, ảnh lật/xoay (xem PNG), sai tỉ lệ, resize khác Python,
chuẩn hóa hai lần, tràn pixel, đảo dấu int8 (tensor PNG sẽ sai màu/âm bản).

Lệnh mẫu (Serial Monitor phải ĐÓNG khi dùng `--port`):

```powershell
.venv\Scripts\python.exe -m pip install pyserial          # một lần
.venv\Scripts\python.exe ESP32_S3_CAM\tools\serial_compare.py --port COM5 --seconds 90 --out serial_out
# hoặc lưu log từ Serial Monitor vào capture.txt rồi:
.venv\Scripts\python.exe ESP32_S3_CAM\tools\serial_compare.py --log capture.txt --out serial_out
```

## 3. Test case chấp nhận

| ID | Tình huống | Cách thực hiện | Kết quả mong muốn / bằng chứng trong log | Trạng thái |
|---|---|---|---|---|
| TC01 | Bật nguồn, không có vật | cấp nguồn | `[MODEL] Tensor allocation successful`, `[SYSTEM] READY`, hai LED tắt | Chưa kiểm thử |
| TC02 | Khoai tây đạt | đặt củ tốt trước cảm biến | `[RESULT] GOOD`, `[LED] GREEN ON` | Chưa kiểm thử |
| TC03 | Khoai tây không đạt | đặt củ hỏng | `[RESULT] NOT_GOOD`, `[LED] RED ON` | Chưa kiểm thử |
| TC04 | Giữ khoai tây trước cảm biến | giữ > 30 s | chỉ **một** `#RESULT`; `[SENSOR] Waiting for removal...`; sau 30 s có cảnh báo `sensor active`; `duplicate triggers=0` | Chưa kiểm thử |
| TC05 | Lấy khoai tây ra | rút vật ra | `[SENSOR] Object removed` → sau cooldown `[SYSTEM] READY FOR NEXT POTATO` | Chưa kiểm thử |
| TC06 | Khoai tây mới | đưa củ khác vào | chu trình mới `cycle #n+1` | Chưa kiểm thử |
| TC07 | Nhiễu ngắn | quẹt tay rất nhanh (< 50 ms) | không có `Potato detected`; `02_Test_Sensor` in `glitch` | Chưa kiểm thử |
| TC08 | Camera chụp thất bại | gõ `1` rồi đưa khoai tây vào (hoặc gõ `m`). **Không** rút cáp camera khi có điện | `[TEST] Injected fault: camera capture failure`, `[ERROR] camera capture failed`, `No GOOD/NOT_GOOD result`, hai LED nháy cùng lúc; không có `#RESULT` | Chưa kiểm thử |
| TC09 | Inference thất bại | (a) gõ `2` rồi kích hoạt một chu trình; (b) tạm đặt `TENSOR_ARENA_SIZE = 64 * 1024` (AllocateTensors lỗi) | (a) `[CNN] ERROR: Invoke() failed`, LED nháy lỗi, không `#RESULT`; (b) `AllocateTensors failed; Invoke() will not be called`, `SAFE STATE` | Chưa kiểm thử |
| TC10 | PSRAM không khả dụng | Tools > PSRAM > Disabled | `[BOOT] ERROR: PSRAM not found -> ...`, `SAFE STATE` | Chưa kiểm thử |
| TC11 | Nhiều chu kỳ | ≥ 50 chu trình, xem `[TEST]` của 06 | `internal RAM ... delta` và `PSRAM ... delta` ổn định (không giảm dần) | Chưa kiểm thử |
| TC12 | Sensor giữ active lâu | chắn cảm biến > 60 s | không chụp liên tục; chỉ cảnh báo | Chưa kiểm thử |
| TC13 | Output không hợp lệ | gõ `3` rồi kích hoạt một chu trình (output bị thay bằng `[127,127]`, tổng xác suất ≈ 2) | `[CNN] ERROR: invalid output raw=[127,127]`, `STATE_ERROR`, không bật LED chất lượng | Chưa kiểm thử |
| TC14 | Khởi động lại | nhấn RESET | log khởi động lặp lại đầy đủ, `Reset reason: external pin`/`power-on` | Chưa kiểm thử |

Ghi chú TC02/TC03: kết quả còn phụ thuộc mô hình và điều kiện ảnh (xem MODEL_INFO.md §8). Nếu sai, dùng
`serial_compare.py` để xác định đó là lỗi firmware hay giới hạn của mô hình trước khi sửa mã.

## 4. Những gì ĐÃ kiểm tra (trên PC, không phải trên ESP32)

Tái lập các kiểm tra C++ trên PC (cần `g++`, ví dụ MSYS2 UCRT64):
`bash ESP32_S3_CAM/tools/host_test/run_host_test.sh`

| Kiểm tra | Công cụ | Kết quả |
|---|---|---|
| Phân tích model: schema, tensor, lượng tử hóa, op + version, header == tflite | `tools/inspect_model.py` | khớp metadata; header giống hệt từng byte |
| Bilinear tham chiếu vs `tf.image.resize` (43 ảnh test thật) | `tools/preprocess_reference.py` | sai lệch 0 |
| Ảnh hưởng của framing lên accuracy (421 ảnh test, khung hình mô phỏng camera) | `tools/framing_study.py` | LETTERBOX 99,76% = pipeline gốc |
| `image_preprocessing.cpp` thật biên dịch bằng g++ (MSYS2), 88 khung × 3 framing | harness host + `preprocess_reference.py` | 0 / 7 299 072 phần tử khác; cả khi bật FMA |
| `#pragma GCC optimize("fp-contract=off")` có cần thiết | như trên, bỏ pragma + `-mfma` | không có pragma → 11 phần tử lệch ±1 |
| CRC32 C++ vs zlib | harness host | khớp |
| `dumpBase64Block()` C++ → `serial_compare.py` (kể cả lỗi cố ý cài vào) | harness host | phát hiện đúng cả hai loại lỗi |
| Cú pháp C++ toàn bộ sketch, `-Wall -Wextra` | g++ + header stub tự viết | 0 lỗi, 0 cảnh báo (stub ≠ API thật, xem README §2) |
| Các tổ hợp cấu hình: JPEG, arena AUTO/INTERNAL, ngưỡng confidence, cooldown=0 | như trên | 0 lỗi |
| `static_assert` chân GPIO (chân camera, chân PSRAM, profile CUSTOM) | như trên | báo lỗi đúng |
| Đồng bộ examples ↔ firmware chính | `tools/sync_project.py --check` | 0 file lệch |
