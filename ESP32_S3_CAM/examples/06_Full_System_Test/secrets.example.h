// secrets.example.h — MẪU. Sao chép thành "secrets.h" (cùng thư mục) rồi điền thông tin thật.
// secrets.h đã nằm trong .gitignore: KHÔNG commit mật khẩu.
// Không có secrets.h → firmware vẫn chạy bình thường nhưng ở chế độ offline (không gửi MQTT).
#pragma once

#define POTATO_WIFI_SSID "ten_wifi"
#define POTATO_WIFI_PASSWORD "mat_khau_wifi"  // ESP32-S3 chỉ dùng Wi-Fi 2.4 GHz

// IPv4 của máy tính chạy Docker (lệnh ipconfig), KHÔNG dùng "localhost".
#define POTATO_MQTT_HOST "192.168.1.100"
#define POTATO_MQTT_PORT 1883
// Phải trùng MQTT_USERNAME / MQTT_PASSWORD trong backend/.env
#define POTATO_MQTT_USERNAME "potato_device"
#define POTATO_MQTT_PASSWORD "mat_khau_trong_backend_env"

// Duy nhất cho từng bo; chỉ dùng chữ, số, '-' hoặc '_' (không có '/').
#define POTATO_DEVICE_ID "potato-01"
