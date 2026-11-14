// =============================================================================================
//  config.h — Toàn bộ tham số cấu hình của firmware (KHÔNG chứa số chân GPIO; xem board_pins.h)
//
//  Các giá trị thời gian là giá trị khởi đầu để thử nghiệm; hãy hiệu chỉnh trên phần cứng thật.
//  File này được sao chép nguyên văn sang examples/ bằng tools/sync_project.py.
// =============================================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------
// Thông tin firmware
// ---------------------------------------------------------------------------------------------
#define FW_NAME     "ESP32-S3 AI Potato Quality Classification System"
#define FW_VERSION  "1.0.0"
#define SERIAL_BAUD 115200
// Thời gian tối đa chờ Serial (USB CDC) sẵn sàng khi khởi động, không chặn vô hạn.
#define SERIAL_WAIT_MS 2000

// ---------------------------------------------------------------------------------------------
// Cảm biến Obstacle Avoidance V1
// ---------------------------------------------------------------------------------------------
// Cực tính tín hiệu OUT khi CÓ vật. Đa số module LM393 (FC-51...) kéo OUT xuống LOW khi có vật,
// nhưng KHÔNG mặc định mọi module đều như vậy: kiểm tra bằng examples/02_Test_Sensor.
#define SENSOR_ACTIVE_LEVEL LOW
// INPUT: module đã có điện trở kéo lên riêng (thường gặp). INPUT_PULLUP/INPUT_PULLDOWN nếu cần.
#define SENSOR_INPUT_MODE INPUT
// Tín hiệu phải ổn định liên tục trong thời gian này mới được coi là "có vật".
#define SENSOR_DEBOUNCE_MS 50
// Tín hiệu phải "không có vật" liên tục trong thời gian này mới được coi là vật đã rời đi.
#define SENSOR_RELEASE_DEBOUNCE_MS 100
// Sau khi vật rời đi, chờ thêm khoảng này trước khi sẵn sàng phân loại vật tiếp theo.
#define SENSOR_COOLDOWN_MS 500
// 1 = cooldown chỉ hoàn tất khi cảm biến TRỐNG LIÊN TỤC suốt SENSOR_COOLDOWN_MS. Nếu vật xuất hiện
//     lại trong cooldown (thường do tín hiệu chập chờn) thì coi là CÙNG một vật → chống phân loại lặp.
// 0 = vật xuất hiện trong cooldown sẽ được phân loại ngay khi cooldown kết thúc.
#define SENSOR_COOLDOWN_REQUIRES_CLEAR 1
// Thời gian chờ sau khi xác nhận có vật rồi mới chụp (để vật dừng hẳn, tránh nhòe).
#define CAMERA_TRIGGER_DELAY_MS 150
// Cảnh báo khi cảm biến active liên tục quá lâu (vật bị kẹt hoặc cảm biến chỉnh sai).
#define SENSOR_STUCK_WARNING_MS 30000
// 1 = nếu khi khởi động cảm biến đang active, KHÔNG phân loại; chờ vật rời đi trước.
#define SENSOR_REQUIRE_CLEAR_AT_BOOT 1

// ---------------------------------------------------------------------------------------------
// LED kết quả
// ---------------------------------------------------------------------------------------------
// Mức logic làm LED SÁNG. HIGH: LED nối GPIO → điện trở → LED → GND. LOW: LED nối 3V3 → ... → GPIO.
#define LED_ACTIVE_LEVEL HIGH
#define LED_RESULT_DURATION_MS 2000
// Báo lỗi hệ thống: hai LED NHẤP NHÁY CÙNG LÚC (khác hẳn GOOD = xanh sáng liên tục,
// NOT_GOOD = đỏ sáng liên tục). Đặt 0 để tắt cả hai LED khi lỗi.
#define LED_ERROR_INDICATION 1
#define LED_ERROR_BLINK_MS 250
// Thời gian hiển thị lỗi runtime (chụp/inference thất bại) trước khi chờ vật rời đi.
#define ERROR_DISPLAY_MS 2000

// ---------------------------------------------------------------------------------------------
// Camera OV3660 (esp32-camera driver đi kèm Arduino-ESP32 Core 3.x)
// ---------------------------------------------------------------------------------------------
#define CAMERA_XCLK_FREQ_HZ 20000000
// Kích thước khung hình (hằng của esp32-camera). QVGA = 320x240, cùng tỉ lệ 4:3 với ảnh gốc của dataset.
#define CAMERA_FRAME_SIZE FRAMESIZE_QVGA
// 0 = RGB565 (MẶC ĐỊNH, không cần giải mã, màu xác định rõ ràng).
// 1 = JPEG (thử nghiệm): giải mã bằng fmt2rgb888() của esp32-camera (cho ra BGR888) rồi mới resize.
#define CAMERA_USE_JPEG 0
#define CAMERA_JPEG_QUALITY 10
// 1 frame buffer trong PSRAM: tiết kiệm bộ nhớ, driver không ghi đè liên tục khi đang giữ frame.
#define CAMERA_FB_COUNT 1
// 1 = báo lỗi nếu cảm biến không phải OV3660 (PID 0x3660). Đặt 0 nếu bo dùng OV2640/OV5640.
#define CAMERA_REQUIRE_OV3660 1
// Số frame bỏ đi sau khi init để AEC/AWB ổn định.
#define CAMERA_WARMUP_FRAMES 8
// Số frame cũ (stale) bỏ đi trước mỗi lần chụp. Với fb_count = 1, frame đang nằm sẵn trong buffer
// được chụp ngay sau lần trả buffer trước (có thể từ trước khi vật xuất hiện) → phải bỏ ít nhất 1.
#define CAMERA_DISCARD_FRAMES 1
#define CAMERA_CAPTURE_RETRIES 3
#define CAMERA_RETRY_DELAY_MS 30
// Sau N lần chụp thất bại liên tiếp sẽ thử deinit/init lại camera (tối đa CAMERA_MAX_REINIT lần).
#define CAMERA_FAILS_BEFORE_REINIT 2
#define CAMERA_MAX_REINIT 3

// Thông số cảm biến ảnh. Mặc định giống Espressif CameraWebServer cho OV3660 (ảnh OV3660 mặc định
// bị lật dọc và hơi quá bão hòa): vflip=1, brightness=+1, saturation=-2. Các giá trị khác để AUTO.
// Hãy kiểm tra hướng/màu ảnh bằng examples/03_Test_Camera + tools/serial_compare.py trước khi đổi.
#define CAM_VFLIP 1
#define CAM_HMIRROR 0
#define CAM_BRIGHTNESS 1        // -2..2
#define CAM_CONTRAST 0          // -2..2
#define CAM_SATURATION (-2)     // -2..2
#define CAM_AUTO_WHITE_BALANCE 1
#define CAM_AWB_GAIN 1
#define CAM_WB_MODE 0           // 0 = Auto (chỉ dùng khi AWB bật)
#define CAM_AUTO_EXPOSURE 1     // AEC
#define CAM_AEC2 0              // AEC DSP
#define CAM_AE_LEVEL 0          // -2..2
#define CAM_AUTO_GAIN 1         // AGC
#define CAM_GAIN_CEILING 2      // 0..6 = 2X..128X (gainceiling_t)

// ---------------------------------------------------------------------------------------------
// Tiền xử lý ảnh (phải khớp tools/preprocess_reference.py)
// ---------------------------------------------------------------------------------------------
#define FRAMING_STRETCH     0   // kéo giãn 320x240 → 96x96 (méo tỉ lệ)
#define FRAMING_LETTERBOX   1   // đệm trên/dưới thành 320x320 rồi resize (giống ảnh 512x512 của dataset)
#define FRAMING_CENTER_CROP 2   // cắt vuông 240x240 ở giữa rồi resize
// LETTERBOX được chọn vì tools/framing_study.py cho thấy nó tái tạo đúng accuracy của pipeline huấn
// luyện trên ảnh test mô phỏng camera (xem MODEL_INFO.md).
#define PREPROC_FRAMING FRAMING_LETTERBOX
// Màu đệm = trung bình 4 ô PATCH x PATCH, lùi INSET pixel vào trong từ 4 góc khung hình.
#define PREPROC_PAD_PATCH 8
#define PREPROC_PAD_INSET 4

// ---------------------------------------------------------------------------------------------
// TensorFlow Lite Micro
// ---------------------------------------------------------------------------------------------
// Cận dưới tính từ vòng đời tensor của potato_int8.tflite: 184 320 byte (96x96x16 + 48x48x16 int8,
// tools/inspect_model.py). Cộng thêm dữ liệu persistent của interpreter và scratch buffer của kernel
// ESP-NN → dùng 320 KB trong PSRAM. Firmware in arena_used_bytes() thực tế sau AllocateTensors();
// hãy chỉnh lại = giá trị đo được + ~20% nếu muốn tiết kiệm.
constexpr size_t TENSOR_ARENA_SIZE = 320 * 1024;
#define ARENA_IN_PSRAM   0
#define ARENA_IN_INTERNAL 1
#define ARENA_AUTO       2   // thử RAM nội trước (nhanh hơn), giữ lại ARENA_INTERNAL_RESERVE byte
#define ARENA_LOCATION ARENA_IN_PSRAM
constexpr size_t ARENA_INTERNAL_RESERVE = 64 * 1024;
// 1 = kiểm tra CRC32 của mảng model nhúng so với giá trị sinh ra từ potato_int8.tflite.
#define MODEL_ENFORCE_CRC 1

// ---------------------------------------------------------------------------------------------
// Quy tắc dự đoán
// ---------------------------------------------------------------------------------------------
// Mặc định: argmax giống train.py (np.argmax). Khi hai điểm bằng nhau, np.argmax chọn chỉ số đầu
// tiên (0 = good) — firmware làm y hệt và in cảnh báo TIE.
#define ENABLE_CONFIDENCE_THRESHOLD 0
#define CONFIDENCE_THRESHOLD 0.60f    // chỉ dùng khi ENABLE_CONFIDENCE_THRESHOLD = 1 → kết quả UNKNOWN
// Tổng xác suất softmax sau giải lượng tử phải ≈ 1. Lệch hơn ngưỡng này = output không hợp lệ.
#define OUTPUT_SUM_TOLERANCE 0.05f

// ---------------------------------------------------------------------------------------------
// Hệ thống / chẩn đoán
// ---------------------------------------------------------------------------------------------
#define INIT_MAX_RETRIES 3            // số lần thử khởi tạo lại phần cứng bị lỗi
#define INIT_RETRY_DELAY_MS 3000
#define FATAL_LOG_INTERVAL_MS 10000   // chu kỳ in lại lỗi khi ở trạng thái an toàn
#define ENABLE_SERIAL_COMMANDS 1      // lệnh 1 ký tự qua Serial Monitor (gõ h để xem)
#define DUMP_TENSOR_EVERY_CYCLE 0     // 1 = luôn xuất tensor INT8 qua Serial (chậm, chỉ để debug)
#define LOG_MEMORY_EVERY_CYCLE 1
