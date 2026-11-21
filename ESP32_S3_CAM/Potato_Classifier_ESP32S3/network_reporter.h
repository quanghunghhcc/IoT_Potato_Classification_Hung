// network_reporter.h — Gửi kết quả phân loại về backend qua Wi-Fi + MQTT (khớp backend/app/mqtt_subscriber.py)
//
// Topic:   agri/<POTATO_DEVICE_ID>/status  "online" (retained, khi kết nối) / "offline" (Last Will, retained)
//          agri/<POTATO_DEVICE_ID>/result  JSON, QoS 1
// JSON:    {"device","quality":"good|not_good|uncertain","confidence","seq","uptime_ms","ts"?,
//           "p_good","p_not_good","raw":[g,n],"capture_ms","pre_ms","inf_ms","fw"}
//
// Nguyên tắc: mạng là phần PHỤ. Phân loại + LED luôn chạy kể cả khi mất Wi-Fi/MQTT.
//   * Không chặn: Wi-Fi tự kết nối lại, ESP-MQTT (có sẵn trong core) chạy trong task riêng.
//   * Mất mạng: kết quả nằm trong hàng đợi (NET_QUEUE_LENGTH) và được gửi bù khi có mạng lại.
//   * Gửi lại có thể tạo bản trùng; backend bỏ trùng theo (device, seq, uptime_ms).
//   * Lỗi hệ thống KHÔNG được gửi như một kết quả (không lẫn với not_good).
// Cấu hình Wi-Fi/MQTT: secrets.h (tạo từ secrets.example.h). Không có secrets.h → mạng tự tắt.
#pragma once

#include <Arduino.h>

#include "cnn_inference.h"

class NetworkReporter {
 public:
  void begin();  // không chặn; chỉ khởi động Wi-Fi
  void loop();   // gọi mỗi vòng loop(): theo dõi Wi-Fi, khởi động MQTT, gửi hàng đợi
  bool enabled() const;
  bool wifiConnected() const;
  bool mqttConnected() const;
  // Đưa một kết quả THÀNH CÔNG vào hàng đợi. Không chặn. false nếu mạng bị tắt.
  bool queueResult(const ClassificationResult& r);
  void printStatus();
  const char* deviceId() const;
};
