# Backend cục bộ

Backend nhận kết quả phân loại từ ESP32 qua MQTT, lưu vào SQLite và hiển thị dashboard tiếng Việt tại máy tính.

## Chạy trên localhost

Cần cài Docker Desktop và bật Docker Compose. Trong PowerShell:

```powershell
cd backend
Copy-Item .env.example .env
notepad .env
docker compose up --build
docker compose down -v
```

Trong `.env`, đổi `MQTT_PASSWORD` thành mật khẩu riêng. Giữ nguyên `MQTT_USERNAME` hoặc dùng cùng một tên ở backend và firmware. Sau khi hai container khởi động, mở:

- Dashboard: <http://localhost:8000>
- API docs: <http://localhost:8000/docs>
- Trạng thái backend/MQTT: <http://localhost:8000/api/health>

Kiểm tra nhanh toàn bộ backend mà chưa cần ESP32 (giả lập đúng bản tin firmware gửi: kết quả hợp lệ, bản trùng,
bản lỗi, mất kết nối đột ngột → Last Will `offline`):

```powershell
docker compose exec backend python -m app.simulate_device
```

Kết quả mong đợi: `RESULT: PASS`. Script tạo một thiết bị tên `sim-xxxxx` trong database; xóa toàn bộ dữ liệu bằng
`docker compose down -v` nếu muốn bảng sạch.

Tuỳ chọn trong `.env`:
- `LOCAL_UTC_OFFSET_HOURS=7`: thống kê theo ngày tính theo giờ Việt Nam (dữ liệu vẫn lưu theo UTC).
- `TELEGRAM_BOT_TOKEN`, `TELEGRAM_CHAT_ID`: gửi cảnh báo Telegram cho mỗi kết quả `not_good` MỚI (bản gửi lại bị
  bỏ trùng nên không báo hai lần). Để trống để tắt.

Sau khi sửa `.env` hoặc mã nguồn: `docker compose up -d --build`.

Dashboard tự tải dữ liệu mới mỗi 5 giây. API gồm `/api/summary`, `/api/events` và `/api/devices`. SQLite và dữ liệu broker được giữ trong Docker volumes qua các lần khởi động. Dừng bằng `Ctrl+C` hoặc `docker compose down`. Lệnh `docker compose down -v` sẽ xóa cả dữ liệu đã lưu.

## Kết nối ESP32

ESP32 không thể dùng `localhost` để tới máy tính; `localhost` trên bo mạch chính là bo mạch. Lấy IPv4 của máy tính bằng `ipconfig` (địa chỉ Wi-Fi/Ethernet cùng mạng với ESP32), sau đó tạo file `ESP32_S3_CAM/Potato_Classifier_ESP32S3/secrets.h` từ `secrets.example.h` và điền:

```cpp
#define POTATO_WIFI_SSID "ten_wifi"
#define POTATO_WIFI_PASSWORD "mat_khau_wifi"
#define POTATO_MQTT_HOST "192.168.x.x" // IPv4 của máy tính chạy Docker
#define POTATO_MQTT_PORT 1883
#define POTATO_MQTT_USERNAME "potato_device"
#define POTATO_MQTT_PASSWORD "mat_khau_trong_backend_env"
```

Giữ `POTATO_DEVICE_ID` duy nhất cho từng bo mạch. Cho phép kết nối TCP cổng 1883 qua Windows Firewall trên mạng riêng nếu ESP32 chưa kết nối được. Cổng dashboard 8000 chỉ mở trên localhost của máy tính.

Firmware (`network_reporter.cpp`) gửi trạng thái tới `agri/<device_id>/status` (`online` khi kết nối, `offline` là
Last Will do broker tự gửi khi bo mất điện/mất mạng, cả hai retained) và kết quả tới `agri/<device_id>/result` ở
QoS 1. Kết quả JSON có `device`, `quality` (`good`, `not_good`, `uncertain`), `confidence` từ 0 đến 1, `seq`,
`uptime_ms`, `ts` (chỉ khi bo lấy được giờ NTP; nếu không backend dùng giờ nhận) và các trường phụ `p_good`,
`p_not_good`, `raw`, `capture_ms`, `pre_ms`, `inf_ms`, `fw` (lưu trong `raw_payload`). Backend bỏ qua bản tin trùng
khi có đủ `seq` và `uptime_ms`. Lỗi hệ thống trên bo không bao giờ được gửi như một kết quả.

Khi mất Wi-Fi/MQTT, bo vẫn phân loại và bật LED bình thường; tối đa 16 kết quả được giữ lại và gửi bù khi có mạng.
Gõ `s` trong Serial Monitor để xem trạng thái mạng (`Network: Wi-Fi ..., MQTT ..., queued ..., sent ...`).

Backend này chỉ hiển thị phân loại, trạng thái thiết bị và thời gian. Không có DHT, dữ liệu độ ẩm/nhiệt độ hoặc buzzer.
