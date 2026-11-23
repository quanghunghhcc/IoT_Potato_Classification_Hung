// =============================================================================================
//  board_pins.h — Sơ đồ chân GPIO
//
//  !!! QUAN TRỌNG !!!
//  Tên module "ESP32-S3-WROOM-1-N16R8" KHÔNG xác định được pinout camera: mỗi nhà sản xuất bo
//  ESP32-S3-CAM có thể đi dây khác nhau. Workspace không có schematic của bo, nên:
//    * Profile mặc định BOARD_PROFILE_ESP32S3_EYE_COMPAT lấy NGUYÊN VĂN từ khối
//      CAMERA_MODEL_ESP32S3_EYE trong camera_pins.h của ví dụ CameraWebServer chính thức
//      (espressif/arduino-esp32). Nhiều bo "ESP32-S3-CAM N16R8" (ví dụ Freenove ESP32-S3-WROOM CAM
//      và các bản sao) dùng chung pinout này, nhưng CHƯA được xác minh với bo của bạn.
//    * Hãy đối chiếu với schematic/tài liệu của nhà sản xuất, rồi đặt BOARD_PINOUT_VERIFIED = 1.
//    * Nếu bo khác, chọn BOARD_PROFILE_CUSTOM và điền đúng số chân.
//  Khi esp_camera_init() thành công và đọc được PID 0x3660 thì chân SCCB/XCLK/PWDN/RESET là đúng;
//  chân dữ liệu D0..D7 sai sẽ cho ảnh nhiễu/sai màu (kiểm tra bằng examples/03_Test_Camera).
// =============================================================================================
#pragma once

#define BOARD_PROFILE_ESP32S3_EYE_COMPAT 1
#define BOARD_PROFILE_CUSTOM             2

#ifndef BOARD_PROFILE
#define BOARD_PROFILE BOARD_PROFILE_ESP32S3_EYE_COMPAT
#endif

// Đặt 1 sau khi đã đối chiếu toàn bộ pinout bên dưới với schematic của bo thật.
#define BOARD_PINOUT_VERIFIED 0

#if BOARD_PROFILE == BOARD_PROFILE_ESP32S3_EYE_COMPAT
#define BOARD_PROFILE_NAME "ESP32S3_EYE_COMPAT (CameraWebServer CAMERA_MODEL_ESP32S3_EYE)"
// Camera OV3660 — CameraWebServer: Y2..Y9 tương ứng D0..D7
#define CAM_PIN_PWDN   -1
#define CAM_PIN_RESET  -1
#define CAM_PIN_XCLK   15
#define CAM_PIN_SIOD    4
#define CAM_PIN_SIOC    5
#define CAM_PIN_D0     11   // Y2
#define CAM_PIN_D1      9   // Y3
#define CAM_PIN_D2      8   // Y4
#define CAM_PIN_D3     10   // Y5
#define CAM_PIN_D4     12   // Y6
#define CAM_PIN_D5     18   // Y7
#define CAM_PIN_D6     17   // Y8
#define CAM_PIN_D7     16   // Y9
#define CAM_PIN_VSYNC   6
#define CAM_PIN_HREF    7
#define CAM_PIN_PCLK   13

#elif BOARD_PROFILE == BOARD_PROFILE_CUSTOM
#define BOARD_PROFILE_NAME "CUSTOM"
// Điền pinout theo schematic của bo, xoá dòng #error, rồi biên dịch lại.
#error "BOARD_PROFILE_CUSTOM: hay dien day du CAM_PIN_* theo schematic cua bo roi xoa dong #error nay."
#define CAM_PIN_PWDN   -1
#define CAM_PIN_RESET  -1
#define CAM_PIN_XCLK   -1
#define CAM_PIN_SIOD   -1
#define CAM_PIN_SIOC   -1
#define CAM_PIN_D0     -1
#define CAM_PIN_D1     -1
#define CAM_PIN_D2     -1
#define CAM_PIN_D3     -1
#define CAM_PIN_D4     -1
#define CAM_PIN_D5     -1
#define CAM_PIN_D6     -1
#define CAM_PIN_D7     -1
#define CAM_PIN_VSYNC  -1
#define CAM_PIN_HREF   -1
#define CAM_PIN_PCLK   -1
#else
#error "BOARD_PROFILE khong hop le"
#endif

// ---------------------------------------------------------------------------------------------
// Cảm biến, LED và OLED — do BẠN đấu dây. Các chân dưới đây được chọn vì (với profile mặc định):
//   * không trùng chân camera (4-13, 15-18),
//   * không thuộc Flash/PSRAM của WROOM-1 N16R8 (GPIO26-32 Flash/PSRAM, GPIO33-37 OPI PSRAM),
//   * không phải chân strapping (0, 3, 45, 46), USB (19, 20) hay UART0 (43, 44),
//   * tránh GPIO38-40 (thường nối khe SD trên bo kiểu Freenove), GPIO2/48 (LED onboard trên nhiều bo).
// OLED I2C dùng GPIO1/GPIO47 theo sơ đồ bo người dùng cung cấp; xác minh chúng có mặt trên header thực tế.
// Kiểm tra lại các chân này có ra header trên bo của bạn không; đổi ở đây nếu cần.
// Các static_assert bên dưới sẽ báo lỗi biên dịch nếu chọn chân xung đột.
// ---------------------------------------------------------------------------------------------
#define SENSOR_PIN     41
#define LED_GREEN_PIN  21
#define LED_RED_PIN    42
#define OLED_SDA_PIN    1
#define OLED_SCL_PIN   47

// ---------------------------------------------------------------------------------------------
// Kiểm tra xung đột chân lúc biên dịch
// ---------------------------------------------------------------------------------------------
#ifdef __cplusplus
namespace board_pin_check {
constexpr int kCameraPins[] = {CAM_PIN_PWDN, CAM_PIN_RESET, CAM_PIN_XCLK, CAM_PIN_SIOD, CAM_PIN_SIOC,
                               CAM_PIN_D0,   CAM_PIN_D1,    CAM_PIN_D2,   CAM_PIN_D3,   CAM_PIN_D4,
                               CAM_PIN_D5,   CAM_PIN_D6,    CAM_PIN_D7,   CAM_PIN_VSYNC, CAM_PIN_HREF,
                               CAM_PIN_PCLK};
constexpr int kCameraPinCount = sizeof(kCameraPins) / sizeof(kCameraPins[0]);

constexpr bool usedByCamera(int pin, int i = 0) {
  return i >= kCameraPinCount ? false : ((pin >= 0 && kCameraPins[i] == pin) || usedByCamera(pin, i + 1));
}
// GPIO26-37: SPI Flash + OPI PSRAM trên ESP32-S3-WROOM-1 N16R8 (không dùng được).
constexpr bool flashOrPsram(int pin) { return pin >= 26 && pin <= 37; }
constexpr bool strapping(int pin) { return pin == 0 || pin == 3 || pin == 45 || pin == 46; }
constexpr bool usbOrUart0(int pin) { return pin == 19 || pin == 20 || pin == 43 || pin == 44; }
constexpr bool validGpio(int pin) { return pin >= 0 && pin <= 48 && !(pin >= 22 && pin <= 25); }
constexpr bool safeUserPin(int pin) {
  return validGpio(pin) && !usedByCamera(pin) && !flashOrPsram(pin) && !strapping(pin) && !usbOrUart0(pin);
}
// Mỗi chân camera (trừ -1) chỉ xuất hiện một lần và không nằm trong vùng Flash/PSRAM.
constexpr bool cameraPinsUnique(int i = 0, int j = 1) {
  return i >= kCameraPinCount ? true
         : j >= kCameraPinCount ? cameraPinsUnique(i + 1, i + 2)
         : ((kCameraPins[i] >= 0 && kCameraPins[i] == kCameraPins[j]) ? false : cameraPinsUnique(i, j + 1));
}
constexpr bool cameraPinsSafe(int i = 0) {
  return i >= kCameraPinCount ? true : (!flashOrPsram(kCameraPins[i]) && cameraPinsSafe(i + 1));
}

static_assert(cameraPinsUnique(), "Hai tin hieu camera dang dung chung mot GPIO");
static_assert(cameraPinsSafe(), "Chan camera nam trong vung Flash/PSRAM (GPIO26-37)");
static_assert(safeUserPin(SENSOR_PIN), "SENSOR_PIN xung dot camera/Flash/PSRAM/strapping/USB/UART0");
static_assert(safeUserPin(LED_GREEN_PIN), "LED_GREEN_PIN xung dot camera/Flash/PSRAM/strapping/USB/UART0");
static_assert(safeUserPin(LED_RED_PIN), "LED_RED_PIN xung dot camera/Flash/PSRAM/strapping/USB/UART0");
static_assert(safeUserPin(OLED_SDA_PIN), "OLED_SDA_PIN xung dot camera/Flash/PSRAM/strapping/USB/UART0");
static_assert(safeUserPin(OLED_SCL_PIN), "OLED_SCL_PIN xung dot camera/Flash/PSRAM/strapping/USB/UART0");
static_assert(SENSOR_PIN != LED_GREEN_PIN && SENSOR_PIN != LED_RED_PIN && LED_GREEN_PIN != LED_RED_PIN &&
                  OLED_SDA_PIN != SENSOR_PIN && OLED_SDA_PIN != LED_GREEN_PIN && OLED_SDA_PIN != LED_RED_PIN &&
                  OLED_SCL_PIN != SENSOR_PIN && OLED_SCL_PIN != LED_GREEN_PIN && OLED_SCL_PIN != LED_RED_PIN &&
                  OLED_SDA_PIN != OLED_SCL_PIN,
              "Sensor, LED va OLED phai dung cac GPIO khac nhau");
}  // namespace board_pin_check
#endif
