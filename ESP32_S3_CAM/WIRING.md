# WIRING — Đấu nối phần cứng

> ⚠ **Pinout camera CHƯA được xác minh với bo của bạn.** Workspace không có schematic. Bảng camera dưới đây
> là khối `CAMERA_MODEL_ESP32S3_EYE` lấy nguyên văn từ `camera_pins.h` của ví dụ CameraWebServer chính thức
> (espressif/arduino-esp32). Nhiều bo "ESP32-S3-CAM N16R8" (Freenove ESP32-S3-WROOM CAM và các bản sao) dùng
> pinout này, nhưng không phải tất cả. Hãy đối chiếu với tài liệu của nhà bán/nhà sản xuất, sau đó đặt
> `BOARD_PINOUT_VERIFIED 1` trong `board_pins.h`. Nếu khác, chọn `BOARD_PROFILE_CUSTOM` và điền số chân.

## 0. Xác nhận module

Đọc dòng chữ trên nắp kim loại của module:

| Ghi trên nắp | Flash | PSRAM | Tools > Flash Mode | Tools > PSRAM |
|---|---|---|---|---|
| **ESP32-S3-WROOM-1 N16R8** (mục tiêu) | 16 MB Quad SPI | 8 MB **Octal** | QIO 80MHz | **OPI PSRAM** |
| ESP32-S3-WROOM-2 N16R8V | 16 MB **Octal** | 8 MB Octal | OPI 80MHz | OPI PSRAM |
| ...-N8R2 / N16R2 | 8/16 MB Quad | 2 MB **Quad** | QIO 80MHz | QSPI PSRAM |

Firmware này được cấu hình cho dòng đầu tiên. `examples/01_Test_PSRAM` xác nhận PSRAM thực tế.

## 1. Camera OV3660 (cáp FPC gắn sẵn trên bo)

| Tín hiệu | GPIO (profile ESP32S3_EYE_COMPAT) | Ghi chú |
|---|---|---|
| PWDN | -1 (không dùng) | |
| RESET | -1 (không dùng) | |
| XCLK | 15 | 20 MHz |
| SIOD (SCCB SDA) | 4 | |
| SIOC (SCCB SCL) | 5 | |
| D0 (Y2) | 11 | |
| D1 (Y3) | 9 | |
| D2 (Y4) | 8 | |
| D3 (Y5) | 10 | |
| D4 (Y6) | 12 | |
| D5 (Y7) | 18 | |
| D6 (Y8) | 17 | |
| D7 (Y9) | 16 | |
| VSYNC | 6 | |
| HREF | 7 | |
| PCLK | 13 | |

Dấu hiệu pinout đúng: log `[CAMERA] Sensor detected: OV3660 (PID 0x3660)` và ảnh xuất bằng
`03_Test_Camera` (lệnh `d`) + `tools/serial_compare.py` có màu/hình hợp lý. PID đọc được ⇒ SCCB/XCLK đúng;
ảnh nhiễu sọc/sai màu ⇒ chân D0–D7/PCLK/HREF/VSYNC sai.

## 2. Cảm biến Obstacle Avoidance V1

| Chân module | Nối tới | Ghi chú |
|---|---|---|
| VCC | **3V3** của bo (khuyến nghị) | xem cảnh báo điện áp |
| GND | GND của bo | GND chung |
| OUT | **GPIO14** (`SENSOR_PIN`) | mặc định active **LOW** (`SENSOR_ACTIVE_LEVEL`), kiểm tra bằng `02_Test_Sensor` |

Chỉnh biến trở trên module để LED của module chỉ sáng khi khoai tây ở đúng vị trí trước camera.

### ⚠ Cảnh báo điện áp

GPIO của ESP32-S3 là **3,3 V, không chịu 5 V**. Module LM393 thường có điện trở kéo OUT lên **VCC của module**:

- Cấp **3,3 V** cho module → OUT tối đa 3,3 V → nối thẳng GPIO14 được (đa số module chạy ở 3,3 V, tầm phát
  hiện có thể ngắn hơn).
- Nếu phải cấp **5 V** → **đo điện áp OUT bằng đồng hồ khi không có vật**. Nếu > 3,6 V thì **không nối thẳng**;
  dùng một trong các cách:
  - cầu phân áp: OUT —[10 kΩ]— GPIO14 —[20 kΩ]— GND (5 V → ~3,3 V);
  - module chuyển mức (level shifter) 5 V ↔ 3,3 V;
  - hoặc tháo điện trở kéo lên trên module và dùng `SENSOR_INPUT_MODE INPUT_PULLUP` (pull-up nội 3,3 V).

## 3. LED kết quả

| LED | GPIO | Đấu nối (LED_ACTIVE_LEVEL = HIGH) |
|---|---|---|
| Xanh — GOOD | **21** (`LED_GREEN_PIN`) | GPIO21 → điện trở 220–330 Ω → Anode LED, Cathode → GND |
| Đỏ — NOT_GOOD | **42** (`LED_RED_PIN`) | GPIO42 → điện trở 220–330 Ω → Anode LED, Cathode → GND |

- Dòng mỗi LED ≈ (3,3 − Vf)/R: LED đỏ (Vf≈2 V) với 330 Ω ≈ 4 mA — an toàn cho GPIO.
- Nếu đấu kiểu 3V3 → điện trở → LED → GPIO thì đặt `LED_ACTIVE_LEVEL LOW`.
- Lỗi hệ thống: cả hai LED **nhấp nháy cùng lúc** — không bao giờ là "đỏ sáng liên tục".

## 4. Nguồn và GND

- Cấp nguồn qua cổng USB-C của bo (5 V) từ cổng USB/nguồn ≥ 1 A. Camera + PSRAM + LED có dòng đỉnh lớn; nguồn yếu
  gây reset `BROWNOUT` (firmware in `Reset reason` khi khởi động).
- **GND chung**: GND của cảm biến, LED và bo phải nối chung.
- Không cấp nguồn ngoài vào chân 3V3 khi đang cắm USB.

## 5. GPIO không nên dùng (ESP32-S3-WROOM-1 N16R8)

| GPIO | Lý do |
|---|---|
| 26–32 | SPI Flash/PSRAM |
| 33–37 | Octal PSRAM (N16R8) — không ra chân |
| 0, 3, 45, 46 | strapping (chế độ boot, điện áp flash) |
| 19, 20 | USB D−/D+ (cổng USB native) |
| 43, 44 | UART0 TX/RX (cổng nạp/Serial qua chip USB-UART) |
| 4–13, 15–18 | camera (profile mặc định) |
| 38–40 | thường nối khe thẻ SD trên bo kiểu Freenove |
| 2, 48 | thường là LED/RGB LED onboard trên nhiều bo |

`board_pins.h` có `static_assert` — biên dịch sẽ **báo lỗi** nếu `SENSOR_PIN`/`LED_*_PIN` trùng camera,
Flash/PSRAM, strapping, USB, UART0, hoặc trùng nhau. Vẫn cần tự kiểm tra chân có ra header trên bo của bạn.
