# TROUBLESHOOTING — Xử lý sự cố

Luôn mở Serial Monitor ở **115200** và đọc các dòng `[ERROR]`, `[BOOT] Reset reason`, `[MODEL]`, `[CAMERA]` trước.

## Camera init failed

Log: `[CAMERA] ERROR: esp_camera_init failed: 0x105 (ESP_ERR_NOT_FOUND)` hoặc `0x...`

1. Pinout sai — nguyên nhân phổ biến nhất. Đối chiếu `board_pins.h` với schematic của bo (WIRING.md §1).
2. Cáp FPC camera lỏng/ngược: tắt nguồn, mở khóa đầu nối, cắm lại đúng chiều, khóa lại.
3. `Tools > PSRAM` phải là **OPI PSRAM**; frame buffer cần PSRAM.
4. Nguồn yếu: dùng cổng USB khác/cáp ngắn, nguồn ≥ 1 A.
5. Lỗi `sensor is not OV3660`: bo dùng cảm biến khác (log in PID). Nếu đúng là OV2640/OV5640, đặt
   `CAMERA_REQUIRE_OV3660 0` — nhưng ảnh sẽ khác dữ liệu huấn luyện hơn nữa.
6. `warm-up capture failed` / `unexpected size`: thử `CAMERA_XCLK_FREQ_HZ 10000000`; kiểm tra D0–D7/PCLK.

## PSRAM not found

Log: `[BOOT] ERROR: PSRAM not found` hoặc `01_Test_PSRAM` báo `PSRAM found: NO`.

- `Tools > PSRAM > OPI PSRAM` (WROOM-1 **N16R8**). Chọn `QSPI PSRAM` cho module R8 là sai và ngược lại.
- Kiểm tra chữ trên module: `N16R8` (OPI PSRAM). `N16R8V`/WROOM-2 cần `Flash Mode: OPI 80MHz`.
- Board nhận PSRAM < 8 MB → sai lựa chọn PSRAM hoặc không phải module R8.

## Tensor allocation failed

Log: `[MODEL] AllocateTensors() returned 1 with arena ... -> tang TENSOR_ARENA_SIZE`

- Tăng `TENSOR_ARENA_SIZE` trong `config.h` (ví dụ `384 * 1024`). Arena ở PSRAM nên có nhiều chỗ.
- `cannot allocate tensor arena`: PSRAM không bật, hoặc `ARENA_LOCATION ARENA_IN_INTERNAL` với arena quá lớn so
  với RAM nội → dùng `ARENA_IN_PSRAM`.
- Sau khi chạy được, ghi lại `Tensor arena used: X` để chỉnh lại arena.

## Missing TensorFlow Lite header

Lỗi biên dịch: `fatal error: tensorflow/lite/micro/micro_interpreter.h: No such file or directory`

- Core `esp32` < 3.0.0 không có TFLM. `Tools > Board > Boards Manager` → cài **esp32 by Espressif Systems
  3.x** (khuyến nghị 3.3.12).
- Board đang chọn không phải ESP32-S3 (ví dụ Arduino Nano ESP32 dùng core khác) → chọn `ESP32S3 Dev Module`.
- **Không** cài/không `#include` các thư viện `TensorFlowLite_ESP32`, `Chirale_TensorFlowLite`,
  `TFLiteMicro_ArduinoESP32S3`, `tflite-micro`... cho project này. Nếu đã cài và gặp lỗi `multiple definition`
  hoặc header lạ, gỡ chúng khỏi `Documents/Arduino/libraries` (hoặc tạm đổi tên thư mục).

## Unsupported operator

Log: `[MODEL]   XYZ  version N  -> NOT SUPPORTED`

- Model đã bị thay bằng model khác (dùng op khác). Mô hình gốc chỉ cần 6 op (MODEL_INFO.md §4).
- Thêm `AddXyz()` tương ứng vào `cnn_inference.cpp` và tăng `kOpCount` — chỉ khi model thực sự đổi.

## Model CRC32 mismatch / model size mismatch

Header model trong sketch khác `models/potato_int8.tflite` → chạy:
`.venv\Scripts\python.exe ESP32_S3_CAM\tools\sync_project.py` rồi biên dịch lại.

## Compilation error

| Thông báo | Cách xử lý |
|---|---|
| `Firmware nay chi danh cho ESP32-S3` | `Tools > Board > ESP32S3 Dev Module` |
| `Can Arduino-ESP32 core >= 3.0.0` | cập nhật core |
| `static assertion failed: SENSOR_PIN xung dot ...` | chân đã chọn trùng camera/Flash/PSRAM/strapping/USB/UART0 — đổi trong `board_pins.h` |
| `BOARD_PROFILE_CUSTOM: hay dien ...` | điền pinout rồi xóa dòng `#error` |
| `potato_model_data.h khong khop model_data.h` | chạy `tools/sync_project.py` |
| `text section exceeds available space` / sketch quá lớn | `Tools > Partition Scheme > 16M Flash (3MB APP/9.9MB FATFS)` |
| lỗi trong file của `examples/` sau khi sửa firmware | chạy `tools/sync_project.py` (examples là bản sao) |

## Upload failed

`Failed to connect to ESP32-S3: No serial data received` / `Wrong boot mode`:

1. Vào chế độ download thủ công: **giữ BOOT**, nhấn và nhả **RESET**, nhả BOOT, rồi Upload lại.
2. Giảm `Tools > Upload Speed` xuống 460800 hoặc 115200.
3. Đóng Serial Monitor/chương trình khác đang chiếm cổng COM.
4. Dùng cáp USB có dữ liệu (không phải cáp chỉ sạc).
5. Nạp qua cổng USB native mà sau đó không thấy cổng: nhấn RESET sau khi nạp xong.

## Không thấy cổng COM

- Cáp USB chỉ sạc → đổi cáp.
- Bo dùng chip USB-UART CH340/CH343/CP2102: cài driver của nhà sản xuất chip.
- Bo có 2 cổng USB-C (thường ghi "USB" và "COM"/"UART"): thử cổng còn lại.
- Cổng USB native (GPIO19/20) chỉ xuất hiện khi firmware chạy hoặc ở chế độ download (giữ BOOT + nhấn RESET).

## Không có Serial output

| Cổng đang cắm | `Tools > USB CDC On Boot` |
|---|---|
| cổng qua chip USB-UART ("COM"/"UART", CH340/CP2102) | **Disabled** |
| cổng USB native của ESP32-S3 ("USB"/"OTG") | **Enabled** |

Sau đó: baud **115200**, nhấn RESET. Firmware chờ Serial tối đa `SERIAL_WAIT_MS` = 2 s rồi vẫn chạy bình thường.
Lưu ý: thông báo nội bộ của TFLM (`MicroPrintf`) đi ra console ESP-IDF (UART0) và có thể không hiện trên cổng USB
native; firmware luôn in lỗi riêng của nó qua `Serial`.

## ESP32 reset liên tục

Xem `[BOOT] Reset reason`:

| Reason | Nguyên nhân thường gặp |
|---|---|
| `BROWNOUT` | nguồn yếu khi camera chạy → nguồn tốt hơn, cáp ngắn, tụ 100–470 µF gần 5V/3V3 |
| `PANIC` | lỗi truy cập bộ nhớ — kiểm tra đã sửa mã chưa; bật `Tools > Core Debug Level > Error` và gửi log |
| `TASK WATCHDOG` / `INTERRUPT WATCHDOG` | một tác vụ chặn quá lâu (không xảy ra với cấu hình mặc định); dump Serial quá dài ở baud thấp |
| `power-on` lặp lại | nguồn chập chờn, tiếp xúc kém |

Sai `Flash Mode` (OPI với module Quad flash) cũng gây boot loop ngay sau khi nạp.

## LED không sáng

1. Chạy `04_Test_LEDs`.
2. Cực LED (chân dài = anode), điện trở 220–330 Ω, GND chung.
3. Sáng ngược (sáng khi phải tắt) → đổi `LED_ACTIVE_LEVEL`.
4. Chân không ra header trên bo của bạn → đổi `LED_GREEN_PIN`/`LED_RED_PIN` (static_assert sẽ kiểm tra xung đột).
5. Hai LED nhấp nháy cùng lúc = **báo lỗi hệ thống** (xem log `[ERROR]`), không phải lỗi LED.

## Sensor không phát hiện

1. Chạy `02_Test_Sensor` và xem `Sensor GPIO:` khi có/không có vật.
2. Mức không đổi → kiểm tra dây OUT/VCC/GND; vặn biến trở trên module (LED của module phải sáng khi có vật).
3. Báo có vật khi trống → đổi `SENSOR_ACTIVE_LEVEL`.
4. Nhiều `glitch` → tăng `SENSOR_DEBOUNCE_MS`; tránh ánh nắng/đèn hồng ngoại chiếu thẳng vào cảm biến.
5. Đo điện áp OUT (WIRING.md §2) — không được > 3,3 V.

## Không gửi được kết quả về backend (MQTT)

Gõ `s` và xem dòng `Network:`.

| Thấy | Cách xử lý |
|---|---|
| `Network: off (no secrets.h)` | tạo `secrets.h` từ `secrets.example.h` trong thư mục sketch rồi nạp lại |
| `Wi-Fi DISCONNECTED` lâu | sai SSID/mật khẩu; ESP32-S3 chỉ dùng Wi-Fi **2.4 GHz**; tránh mạng có trang đăng nhập (captive portal) |
| Wi-Fi connected nhưng `MQTT DISCONNECTED` | `POTATO_MQTT_HOST` phải là IPv4 của máy tính (không phải `localhost`); backend phải đang chạy (`docker compose ps`); mở cổng **TCP 1883** trong Windows Firewall cho mạng Private; user/password khớp `backend/.env` |
| `MQTT errors` tăng, broker log `not authorised` | sai `POTATO_MQTT_USERNAME/PASSWORD`; sau khi đổi `.env` chạy `docker compose up -d` |
| Lỗi biên dịch `mqtt_client.h: No such file` | core quá cũ hoặc không phải ESP32; dùng core 3.x |
| `queued` tăng mãi, `sent` không tăng | broker không nhận QoS 1; xem `docker compose logs mosquitto` |

Dashboard vẫn báo `online` sau khi rút điện bo: chờ khoảng 45 s (1,5 × keepalive 30 s) để broker gửi Last Will.

## CNN luôn dự đoán một class

Làm theo thứ tự để tách lỗi firmware khỏi giới hạn mô hình:

1. `05_Test_CNN`: Part A FAIL → lỗi runtime/model; Part B FAIL → lỗi tiền xử lý. Cả hai PASS → firmware tính đúng.
2. Firmware chính: gõ `t`, `f`, rồi kích hoạt một chu trình; phân tích bằng `tools/serial_compare.py`.
   - `[PREPROC] ... IDENTICAL` và `[TENSOR] ... OK` → firmware khớp Python trên ảnh thật.
   - Xem `frame_<seq>.png` / `tensor_<seq>.png`: ảnh tối? lệch màu? lật? khoai tây quá nhỏ/ngoài khung?
3. Luôn ra **NOT_GOOD**: mô hình dự đoán NOT_GOOD cho ảnh không có khoai tây, ảnh đơn sắc và nền trống
   (MODEL_INFO.md §8). Hãy để khoai tây chiếm phần lớn khung hình như trong dataset, chiếu sáng đều.
4. Luôn ra **GOOD**: dataset có nền trắng gần như chỉ ở lớp good → nền trắng có thể kéo về GOOD.
5. Nếu ảnh hợp lý mà vẫn sai: đó là giới hạn của dữ liệu huấn luyện (nền/nguồn ảnh tương quan với nhãn). Cách
   khắc phục đúng là thu thập ảnh bằng chính camera OV3660 trong điều kiện vận hành và huấn luyện lại —
   **không** sửa firmware để "ép" kết quả.
