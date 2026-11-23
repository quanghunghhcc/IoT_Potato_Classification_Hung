# ESP32-S3 AI Potato Quality Classification System

Firmware Arduino cho **ESP32-S3-CAM (ESP32-S3-WROOM-1 N16R8) + camera OV3660 + cảm biến Obstacle Avoidance V1 +
2 LED + OLED I2C 0.96 inch tùy chọn**. Khi cảm biến phát hiện khoai tây, camera chụp một ảnh, ESP32-S3 chạy **CNN INT8 (`models/potato_int8.tflite`)
bằng TensorFlow Lite Micro ngay trên chip**, rồi bật LED xanh (GOOD) hoặc đỏ (NOT_GOOD). Hoạt động hoàn toàn
offline ở phần phân loại; có thể bật Wi-Fi/MQTT để gửi kết quả và ảnh về backend. OLED hiển thị trạng thái và kết quả.

Tài liệu đi kèm: [WIRING.md](WIRING.md) · [MODEL_INFO.md](MODEL_INFO.md) · [TESTING.md](TESTING.md) ·
[TROUBLESHOOTING.md](TROUBLESHOOTING.md)

## 1. Đọc trước: rủi ro cần biết

1. **Pinout camera chưa xác minh** với bo của bạn (không có schematic trong workspace). Mặc định dùng pinout
   `CAMERA_MODEL_ESP32S3_EYE` của ví dụ CameraWebServer chính thức. Xem WIRING.md trước khi cấp nguồn.
2. **Firmware chưa được biên dịch bằng toolchain ESP32 và chưa chạy trên phần cứng** (xem §2).
3. **Dữ liệu huấn luyện có tương quan nền ↔ nhãn**: toàn bộ ảnh `not_good` là ảnh 512×512 nền bê tông có dải pad
   xám; phần lớn ảnh `good` nền trắng, nguồn khác. Accuracy 99,76% đo trên cùng phân phối — ngoài thực tế có thể
   thấp hơn nhiều. Ảnh không có khoai tây bị dự đoán NOT_GOOD. Chi tiết và số liệu: MODEL_INFO.md §8–9.

## 2. Trạng thái thực hiện

| Hạng mục | Trạng thái | Bằng chứng |
|---|---|---|
| Viết mã nguồn firmware + 6 sketch kiểm thử + công cụ Python | **Đã xong** | thư mục này |
| Phân tích model (op, version, lượng tử hóa, header == tflite) | **Đã chạy** | `tools/inspect_model.py` → `tools/model_report.json` |
| Tiền xử lý C++ tương đương Python/TensorFlow | **Đã kiểm tra trên PC** (g++), 0 phần tử khác trên 88 khung × 3 framing | TESTING.md §4 |
| Kiểm tra cú pháp C++ mọi file, `-Wall -Wextra` | **Đã chạy trên PC** với header *stub* do tôi viết — chỉ bắt lỗi C++, **không** chứng minh khớp API thật của core | TESTING.md §4 |
| Biên dịch bằng Arduino IDE / arduino-cli + core ESP32 | **CHƯA** — máy không có Arduino IDE/CLI; việc tải arduino-cli trong phiên làm việc bị chặn | chạy `tools/compile_all.ps1` |
| Nạp firmware | **CHƯA** | — |
| Kiểm thử trên phần cứng (TC01–TC14) | **CHƯA** | TESTING.md §3 |

## 3. Sơ đồ hoạt động (state machine)

```
 STATE_INIT ──lỗi──► STATE_ERROR (log + 2 LED nháy cùng lúc; thử lại có giới hạn; không phân loại)
     │ PSRAM → LED(tắt) → cảm biến → camera OV3660 → TFLM (model, op, arena, AllocateTensors, kiểm tra tensor)
     ▼
 STATE_IDLE ── LED tắt, chỉ đọc cảm biến (debounce 50 ms) ──► có vật (một lần "arrival" mới)
     ▼
 STATE_OBJECT_DETECTED ── chờ 150 ms cho vật đứng yên (vật rời đi sớm → huỷ, không phân loại)
     ▼
 STATE_CAMERA_CAPTURE ── bỏ frame cũ, chụp QVGA RGB565, kiểm tra, retry ≤ 3
     ▼
 STATE_PREPROCESSING ── RGB565→RGB888 → LETTERBOX 320×320 → bilinear 96×96 → INT8 vào input tensor → trả frame
     ▼
 STATE_INFERENCE ── Invoke() → giải lượng tử → kiểm tra hợp lệ → argmax (index 0 good, 1 not_good)
     ▼
 STATE_SHOW_RESULT ── GOOD: LED xanh / NOT_GOOD: LED đỏ trong 2 s
     ▼
 STATE_WAIT_REMOVAL ── KHÔNG phân loại lại; chờ vật rời đi (debounce 100 ms) + trống liên tục 500 ms
     └──────────────► STATE_IDLE
 Lỗi chụp/inference trong chu trình → STATE_ERROR (2 s) → STATE_WAIT_REMOVAL (không thử lại trên cùng vật)
```

## 4. Cấu trúc project

```
ESP32_S3_CAM/
├── Potato_Classifier_ESP32S3/        ← FIRMWARE CHÍNH (mở file .ino này)
│   ├── Potato_Classifier_ESP32S3.ino   setup()/loop() — chỉ điều phối
│   ├── config.h                        mọi tham số (thời gian, camera, framing, arena, ...)
│   ├── board_pins.h                    GPIO + static_assert chống xung đột chân
│   ├── system_controller.h/.cpp        state machine, chống lặp, xử lý lỗi, lệnh Serial
│   ├── sensor_module.h/.cpp            đọc cảm biến, debounce không chặn
│   ├── camera_module.h/.cpp            esp32-camera: init, chụp, kiểm tra frame, trả buffer
│   ├── image_preprocessing.h/.cpp      RGB565→INT8 96×96 (khớp Python bit-exact)
│   ├── cnn_inference.h/.cpp            TFLM: kiểm tra model, resolver 6 op, arena, Invoke, hậu xử lý
│   ├── led_control.h/.cpp              LED + chỉ báo lỗi riêng biệt
│   ├── oled_module.h/.cpp              OLED SSD1306/SH1106 I2C, không cần thư viện ngoài
│   ├── diagnostics.h/.cpp              CRC32, dump base64, thông tin bộ nhớ
│   ├── model_data.h / model_data.cpp   khai báo extern + TU duy nhất include model
│   └── potato_model_data.h             BẢN SAO NGUYÊN VĂN models/potato_model_data.h
├── examples/                         ← mỗi thư mục là một sketch độc lập
│   ├── 01_Test_PSRAM/  02_Test_Sensor/  03_Test_Camera/
│   ├── 04_Test_LEDs/   05_Test_CNN/ (+ test_vectors.*)  06_Full_System_Test/
├── tools/                            ← Python (chạy bằng .venv của project)
│   ├── inspect_model.py        phân tích potato_int8.tflite
│   ├── preprocess_reference.py bản NumPy của image_preprocessing.cpp
│   ├── framing_study.py        ảnh hưởng framing / nền lên dự đoán
│   ├── export_test_vectors.py  sinh test_vectors.* cho 05_Test_CNN từ dataset/test
│   ├── serial_compare.py       so sánh dump Serial của ESP32 với TFLite/NumPy trên PC
│   ├── sync_project.py         đồng bộ model + module dùng chung sang examples
│   ├── compile_all.ps1         biên dịch tất cả bằng arduino-cli
│   └── host_test/              chạy image_preprocessing.cpp thật trên PC (g++) so với Python
└── README.md  WIRING.md  MODEL_INFO.md  TESTING.md  TROUBLESHOOTING.md
```

Các file trong `examples/*` (trừ `.ino` và `test_vectors.*`) là **bản sao** của firmware chính để mỗi sketch tự
đứng được (Arduino IDE không hỗ trợ include tương đối ra ngoài thư mục sketch). **Chỉ sửa trong
`Potato_Classifier_ESP32S3/`**, rồi chạy `.venv\Scripts\python.exe ESP32_S3_CAM\tools\sync_project.py`.

## 5. Cài đặt Arduino IDE

### 5.1 Arduino IDE
Cài **Arduino IDE 2.x** từ https://www.arduino.cc/en/software.

### 5.2 Board package ESP32
1. `File > Preferences > Additional boards manager URLs`, thêm:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
2. `Tools > Board > Boards Manager`, tìm **esp32 by Espressif Systems**, cài **3.3.12**.
   - 3.3.12 là bản ổn định mới nhất (18/09/2026, ESP-IDF v5.5.5) tại thời điểm viết. Mã nguồn **chưa được
     biên dịch** với bản này (§2). Không dùng bản `4.0.0-RC*` (pre-release, ESP-IDF 6.1, chưa kiểm tra).
   - Yêu cầu tối thiểu: **3.0.0** (core 2.x không có TFLM — firmware dừng biên dịch với thông báo rõ ràng).

### 5.3 Thư viện
**Không cần cài thêm thư viện nào.**
- TensorFlow Lite Micro: dùng bản `esp-tflite-micro` **tích hợp sẵn trong core 3.x** (có kernel tối ưu ESP-NN).
  Kiểm tra: biên dịch `examples/05_Test_CNN` — nếu báo thiếu `tensorflow/lite/micro/micro_interpreter.h` thì core
  < 3.0 hoặc chưa chọn board ESP32-S3.
- Camera: `esp_camera.h` (esp32-camera) cũng đi kèm core.
- Tránh xung đột: **không** cài/`#include` `TensorFlowLite_ESP32`, `Chirale_TensorFlowLite`,
  `TFLiteMicro_ArduinoESP32S3`, `tflite-micro`, `EloquentTinyML`... cho project này. Nếu đã cài và gặp lỗi lạ,
  tạm chuyển chúng ra khỏi `Documents\Arduino\libraries`.
- Lý do chọn và so sánh các thư viện: MODEL_INFO.md §5.

### 5.4 Cấu hình board (menu Tools)

| Mục | Giá trị |
|---|---|
| Board | **ESP32S3 Dev Module** |
| Flash Size | **16MB (128Mb)** |
| Flash Mode | **QIO 80MHz** (WROOM-1 dùng flash Quad; *OPI* chỉ dành cho WROOM-2 / N16R8**V**) |
| PSRAM | **OPI PSRAM** |
| Partition Scheme | **16M Flash (3MB APP/9.9MB FATFS)** (dư chỗ; kích thước firmware thực tế chưa đo) |
| CPU Frequency | **240MHz (WiFi)** |
| USB CDC On Boot | **Disabled** nếu cắm cổng qua chip USB-UART (CH340/CP2102, thường ghi "COM"/"UART"); **Enabled** nếu cắm cổng USB native ("USB"/"OTG") |
| USB Mode | Hardware CDC and JTAG |
| Upload Mode | UART0 / Hardware CDC |
| Upload Speed | 921600 (giảm xuống 460800/115200 nếu nạp lỗi) |
| Core Debug Level | None (đặt Error khi cần chẩn đoán camera) |
| Port | cổng COM của bo |

Không có cấu hình USB nào đúng cho mọi bo: nếu không thấy log, đổi `USB CDC On Boot` hoặc đổi cổng cắm.

### 5.5 Biên dịch
1. Kiểm tra/sửa `board_pins.h` theo bo của bạn (WIRING.md).
2. `File > Open` → `ESP32_S3_CAM/Potato_Classifier_ESP32S3/Potato_Classifier_ESP32S3.ino`.
3. Nhấn **Verify**. Lỗi thường gặp: TROUBLESHOOTING.md "Compilation error".
4. (Tuỳ chọn) biên dịch tất cả bằng arduino-cli: `powershell -ExecutionPolicy Bypass -File ESP32_S3_CAM\tools\compile_all.ps1`.

### 5.6 Nạp firmware
1. Cắm USB, chọn đúng `Tools > Port`, nhấn **Upload**.
2. Nếu báo `Failed to connect`: giữ **BOOT**, nhấn-nhả **RESET**, nhả **BOOT**, Upload lại. Sau khi nạp xong nhấn
   **RESET** để chạy.

### 5.7 Theo dõi
`Tools > Serial Monitor`, baud **115200**, nhấn RESET. Log khởi động mong đợi (giá trị thật sẽ khác):

```
[BOOT] PSRAM detected: ... B total
[SENSOR] Sensor ready
[CAMERA] Sensor detected: OV3660 (PID 0x3660)
[CAMERA] Camera ready
[MODEL] Model loaded
[MODEL] Input shape: [1,96,96,3]   Input type: INT8   Output shape: [1,2]   Output type: INT8
[MODEL] Tensor allocation successful
[MODEL] Tensor arena used: <đo được> / 327680 B
[SYSTEM] READY
[SYSTEM] Waiting for potato...
```

### 5.8 Demo OLED và phân loại

1. Đấu OLED I2C 4 chân theo [WIRING.md](WIRING.md#4-oled-096-inch-i2c-ssd1306sh1106-128x64): VCC→3V3, GND→GND,
   SDA→GPIO1, SCL→GPIO47. OLED dùng driver tích hợp, không cần cài thư viện Arduino ngoài.
2. Mở sketch `ESP32_S3_CAM/Potato_Classifier_ESP32S3/Potato_Classifier_ESP32S3.ino`, chọn board/PSRAM/Flash như mục 5,
   nhấn **Verify**, rồi **Upload**. OLED bật sẵn qua `ENABLE_OLED 1` trong `config.h`.
3. Mở Serial Monitor ở **115200 baud**. Màn hình hiện trạng thái khởi động, chờ khoai, đang chụp/nhận dạng và kết quả
   `GOOD`, `NOT GOOD` hoặc `UNKNOWN` cùng độ tin cậy. Đặt khoai trước cảm biến để chạy tự động.
4. Để demo không cần cảm biến, gửi ký tự `m` trong Serial Monitor khi màn hình đang ở trạng thái chờ; firmware chụp và
   phân loại ngay. Lấy vật khỏi vùng cảm biến trước lần chạy tiếp theo.
5. Kết quả vẫn được in ở Serial Monitor và điều khiển LED xanh/đỏ. Nếu OLED không được nối, xem log `[OLED]` để kiểm tra;
   phần phân loại vẫn tiếp tục.

## 6. Sử dụng

- Đặt khoai tây trước cảm biến sao cho nó **chiếm phần lớn khung hình camera**, nền đồng màu, ánh sáng đều
  (giống ảnh huấn luyện). Mỗi lần vật đi vào = đúng một lần phân loại; phải lấy vật ra để phân loại củ tiếp theo.
- Lệnh Serial (gõ một ký tự): `h` trợ giúp · `s` trạng thái/thống kê/bộ nhớ · `t` xuất tensor của chu trình kế ·
  `f` xuất ảnh camera của chu trình kế · `m` kích hoạt thủ công (khi IDLE) · `r` xoá thống kê ·
  `1`/`2`/`3` chèn lỗi chụp / Invoke / output cho kiểm thử TC08/TC09/TC13.
- Mỗi lần phân loại in một dòng máy đọc được:
  `#RESULT,seq=..,pred=GOOD,raw_good=..,raw_not_good=..,p_good=..,p_not_good=..,capture_ms=..,pre_ms=..,inf_ms=..,total_ms=..,input_crc32=..`

### Tham số chính (`config.h`)

| Tham số | Mặc định | Ý nghĩa |
|---|---|---|
| `SENSOR_ACTIVE_LEVEL` | `LOW` | mức OUT khi có vật |
| `SENSOR_DEBOUNCE_MS` / `SENSOR_RELEASE_DEBOUNCE_MS` | 50 / 100 | chống nhiễu khi vào / ra |
| `SENSOR_COOLDOWN_MS` | 500 | thời gian trống tối thiểu trước khi nhận vật mới |
| `CAMERA_TRIGGER_DELAY_MS` | 150 | chờ vật đứng yên trước khi chụp |
| `LED_ACTIVE_LEVEL` / `LED_RESULT_DURATION_MS` | `HIGH` / 2000 | cực tính / thời gian giữ LED |
| `CAMERA_USE_JPEG` | 0 (RGB565) | 1 = JPEG + giải mã (thử nghiệm) |
| `CAM_VFLIP`, `CAM_BRIGHTNESS`, `CAM_SATURATION`... | 1, 1, −2 | theo khuyến nghị OV3660 của CameraWebServer |
| `PREPROC_FRAMING` | `FRAMING_LETTERBOX` | xem MODEL_INFO.md §9 |
| `TENSOR_ARENA_SIZE` / `ARENA_LOCATION` | 320 KB / PSRAM | xem MODEL_INFO.md §6 |
| `ENABLE_CONFIDENCE_THRESHOLD` | 0 | 1 = dưới ngưỡng → UNKNOWN (không bật LED chất lượng) |

**Vì sao RGB565 thay vì JPEG?** RGB565 không cần giải mã (độ trễ thấp hơn, không tốn thêm buffer RGB888 230 KB),
không có nhiễu nén, thứ tự byte/màu xác định rõ và đã được kiểm chứng bit-exact với Python; chi phí là frame
buffer 150 KB trong PSRAM (dư thừa với 8 MB). JPEG thêm một bước giải mã có hành vi khác decoder của TensorFlow
và hàm `fmt2rgb888()` cho ra BGR — được hỗ trợ (`CAMERA_USE_JPEG 1`) nhưng không phải mặc định.

## 6b. Gửi kết quả về backend (Wi-Fi + MQTT)

1. Chạy backend trước (xem `backend/README.md`, `docker compose up -d --build`).
2. Lấy IPv4 của máy tính bằng `ipconfig` (cùng mạng Wi-Fi 2.4 GHz với ESP32).
3. Trong `Potato_Classifier_ESP32S3/`, sao chép `secrets.example.h` thành **`secrets.h`** và điền SSID, mật khẩu
   Wi-Fi, `POTATO_MQTT_HOST` = IPv4 ở bước 2, `POTATO_MQTT_USERNAME/PASSWORD` = giá trị trong `backend/.env`,
   `POTATO_DEVICE_ID` duy nhất cho mỗi bo. `secrets.h` đã được `.gitignore`.
4. Nạp lại firmware. Log mong đợi: `[NET] Wi-Fi connected: IP ...`, `[NET] MQTT connected (status: online)`, và sau
   mỗi lần phân loại `[NET] Result queued for MQTT`. Dashboard: http://localhost:8000.

- Dùng ESP-MQTT **có sẵn trong core** (`mqtt_client.h`), không cần cài thư viện.
- Không có `secrets.h` hoặc `ENABLE_NETWORK 0` → firmware chạy offline như trước.
- Mạng là phần phụ: mất Wi-Fi/MQTT không ảnh hưởng phân loại/LED; tối đa `NET_QUEUE_LENGTH` = 16 kết quả chờ gửi bù.
- Lỗi hệ thống (chụp/Invoke/output lỗi) **không** được gửi như kết quả.
- Wi-Fi tăng dòng tiêu thụ: nếu bo reset với `Reset reason: BROWNOUT`, dùng nguồn/cáp USB tốt hơn.
- Sketch `06_Full_System_Test` cũng gửi MQTT nếu có `secrets.h` (được `tools/sync_project.py` sao chép sang).

## 7. Kiểm thử

Thứ tự: `01` PSRAM → `04` LED → `02` cảm biến → `03` camera → `05` CNN → `06` toàn hệ thống → firmware chính.
Kế hoạch đầy đủ, test case TC01–TC14 và quy trình so sánh Python ↔ ESP32: [TESTING.md](TESTING.md).

## 8. Công cụ Python

Chạy từ thư mục project bằng môi trường có sẵn (`.venv`, TensorFlow 2.21):

```powershell
.venv\Scripts\python.exe ESP32_S3_CAM\tools\inspect_model.py          # phân tích model
.venv\Scripts\python.exe ESP32_S3_CAM\tools\sync_project.py --check   # examples có khớp firmware chính?
.venv\Scripts\python.exe ESP32_S3_CAM\tools\export_test_vectors.py    # sinh lại test vector (sau khi train lại)
.venv\Scripts\python.exe ESP32_S3_CAM\tools\framing_study.py          # nghiên cứu framing / nền
.venv\Scripts\python.exe ESP32_S3_CAM\tools\serial_compare.py --log capture.txt --out serial_out
```

Sau khi huấn luyện lại bằng `train.py`: chạy `sync_project.py` → `export_test_vectors.py` → biên dịch, nạp lại.
