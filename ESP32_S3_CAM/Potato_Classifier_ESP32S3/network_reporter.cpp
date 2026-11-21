// network_reporter.cpp — xem network_reporter.h
#include "network_reporter.h"

#include "config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#define NET_HAS_SECRETS 1
#else
#define NET_HAS_SECRETS 0
#endif

#if ENABLE_NETWORK && NET_HAS_SECRETS
#define NET_ACTIVE 1
#include <WiFi.h>
#include <time.h>

#include <atomic>

#include "mqtt_client.h"  // ESP-MQTT của ESP-IDF, có sẵn trong Arduino-ESP32 core 3.x
#else
#define NET_ACTIVE 0
#endif

#ifndef POTATO_DEVICE_ID
#define POTATO_DEVICE_ID "potato-01"
#endif
#ifndef POTATO_MQTT_PORT
#define POTATO_MQTT_PORT 1883
#endif

#if NET_ACTIVE
namespace {

constexpr size_t kMsgLen = 448;

// Hàng đợi kết quả (chỉ loop() truy cập → không cần khóa).
char gQueue[NET_QUEUE_LENGTH][kMsgLen];
uint16_t gQueueBytes[NET_QUEUE_LENGTH];
int gHead = 0;
int gCount = 0;

// Trạng thái do task MQTT ghi, loop() đọc.
std::atomic<bool> gMqttConnected{false};
std::atomic<int> gAckedMsgId{-1};
std::atomic<uint32_t> gMqttConnects{0};
std::atomic<uint32_t> gMqttErrors{0};

esp_mqtt_client_handle_t gClient = nullptr;
bool gMqttStarted = false;
bool gWifiWasConnected = false;
bool gNtpStarted = false;
uint32_t gWifiLastBeginMs = 0;
int gInflightMsgId = -1;
uint32_t gInflightSinceMs = 0;
uint32_t gSent = 0;
uint32_t gDropped = 0;
uint32_t gWifiReconnects = 0;
bool gLastMqttState = false;

char gStatusTopic[96];
char gResultTopic[96];
char gUri[128];

void mqttEventHandler(void*, esp_event_base_t, int32_t eventId, void* eventData) {
  const esp_mqtt_event_handle_t ev = static_cast<esp_mqtt_event_handle_t>(eventData);
  switch (static_cast<esp_mqtt_event_id_t>(eventId)) {
    case MQTT_EVENT_CONNECTED:
      gMqttConnected = true;
      ++gMqttConnects;
      // QoS 0 cho "online" để sự kiện PUBLISHED chỉ thuộc về bản tin kết quả (QoS 1).
      esp_mqtt_client_publish(ev->client, gStatusTopic, "online", 0, 0, 1);
      break;
    case MQTT_EVENT_DISCONNECTED:
      gMqttConnected = false;
      break;
    case MQTT_EVENT_PUBLISHED:
      gAckedMsgId = ev->msg_id;
      break;
    case MQTT_EVENT_ERROR:
      ++gMqttErrors;
      break;
    default:
      break;
  }
}

void startMqtt() {
  esp_mqtt_client_config_t cfg = {};
  cfg.broker.address.uri = gUri;
  cfg.credentials.client_id = POTATO_DEVICE_ID;
  cfg.credentials.username = POTATO_MQTT_USERNAME;
  cfg.credentials.authentication.password = POTATO_MQTT_PASSWORD;
  cfg.session.keepalive = NET_MQTT_KEEPALIVE_S;
  cfg.session.last_will.topic = gStatusTopic;
  cfg.session.last_will.msg = "offline";
  cfg.session.last_will.msg_len = 7;
  cfg.session.last_will.qos = 1;
  cfg.session.last_will.retain = 1;
  cfg.network.reconnect_timeout_ms = 5000;
  cfg.network.timeout_ms = 5000;

  gClient = esp_mqtt_client_init(&cfg);
  if (gClient == nullptr) {
    Serial.println("[NET] ERROR: esp_mqtt_client_init failed");
    return;
  }
  esp_mqtt_client_register_event(gClient, MQTT_EVENT_ANY, mqttEventHandler, nullptr);
  if (esp_mqtt_client_start(gClient) != ESP_OK) {
    Serial.println("[NET] ERROR: esp_mqtt_client_start failed");
    return;
  }
  gMqttStarted = true;
  Serial.printf("[NET] MQTT client started -> %s (user %s, client id %s)\n", gUri, POTATO_MQTT_USERNAME,
                POTATO_DEVICE_ID);
}

void popQueue() {
  gHead = (gHead + 1) % NET_QUEUE_LENGTH;
  --gCount;
}

}  // namespace
#endif  // NET_ACTIVE

bool NetworkReporter::enabled() const { return NET_ACTIVE != 0; }
const char* NetworkReporter::deviceId() const { return POTATO_DEVICE_ID; }

bool NetworkReporter::wifiConnected() const {
#if NET_ACTIVE
  return WiFi.status() == WL_CONNECTED;
#else
  return false;
#endif
}

bool NetworkReporter::mqttConnected() const {
#if NET_ACTIVE
  return gMqttConnected;
#else
  return false;
#endif
}

void NetworkReporter::begin() {
#if !ENABLE_NETWORK
  Serial.println("[NET] Network disabled (ENABLE_NETWORK = 0) -> offline mode");
#elif !NET_HAS_SECRETS
  Serial.println("[NET] secrets.h not found -> offline mode (copy secrets.example.h to secrets.h to enable MQTT)");
#else
  snprintf(gStatusTopic, sizeof(gStatusTopic), "%s/%s/status", NET_TOPIC_PREFIX, POTATO_DEVICE_ID);
  snprintf(gResultTopic, sizeof(gResultTopic), "%s/%s/result", NET_TOPIC_PREFIX, POTATO_DEVICE_ID);
  snprintf(gUri, sizeof(gUri), "mqtt://%s:%d", POTATO_MQTT_HOST, (int)POTATO_MQTT_PORT);
  Serial.printf("[NET] Device ID: %s, topics %s | %s\n", POTATO_DEVICE_ID, gStatusTopic, gResultTopic);
  Serial.printf("[NET] Connecting Wi-Fi \"%s\" (non-blocking)...\n", POTATO_WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(POTATO_DEVICE_ID);
  WiFi.setAutoReconnect(true);
  WiFi.begin(POTATO_WIFI_SSID, POTATO_WIFI_PASSWORD);
  gWifiLastBeginMs = millis();
#endif
}

void NetworkReporter::loop() {
#if NET_ACTIVE
  const uint32_t now = millis();
  const bool wifiUp = WiFi.status() == WL_CONNECTED;

  if (wifiUp != gWifiWasConnected) {
    gWifiWasConnected = wifiUp;
    if (wifiUp) {
      Serial.printf("[NET] Wi-Fi connected: IP %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(),
                    (int)WiFi.RSSI());
#if NET_ENABLE_NTP
      if (!gNtpStarted) {
        configTime(0, 0, NET_NTP_SERVER1, NET_NTP_SERVER2);  // giờ UTC; không có Internet thì bỏ "ts"
        gNtpStarted = true;
      }
#endif
      if (!gMqttStarted) startMqtt();  // ESP-MQTT tự kết nối lại sau đó
    } else {
      Serial.println("[NET] Wi-Fi disconnected (classification continues offline)");
    }
  }
  // Dự phòng khi auto-reconnect không tự hồi phục.
  if (!wifiUp && now - gWifiLastBeginMs >= NET_WIFI_RETRY_MS) {
    gWifiLastBeginMs = now;
    ++gWifiReconnects;
    WiFi.disconnect();
    WiFi.begin(POTATO_WIFI_SSID, POTATO_WIFI_PASSWORD);
  }

  const bool mqttUp = gMqttConnected;
  if (mqttUp != gLastMqttState) {
    gLastMqttState = mqttUp;
    Serial.println(mqttUp ? "[NET] MQTT connected (status: online)" : "[NET] MQTT disconnected");
  }

  // Gửi hàng đợi: tối đa 1 bản tin đang chờ PUBACK.
  if (gInflightMsgId >= 0) {
    if (gAckedMsgId == gInflightMsgId) {
      popQueue();
      ++gSent;
      gInflightMsgId = -1;
    } else if (!mqttUp || now - gInflightSinceMs >= NET_ACK_TIMEOUT_MS) {
      gInflightMsgId = -1;  // gửi lại sau; backend tự bỏ bản trùng
    }
  }
  if (gInflightMsgId < 0 && mqttUp && gCount > 0 && gClient != nullptr) {
    const int id = esp_mqtt_client_enqueue(gClient, gResultTopic, gQueue[gHead], gQueueBytes[gHead], 1, 0, true);
    if (id >= 0) {
      gInflightMsgId = id;
      gInflightSinceMs = now;
    }
  }
#endif
}

bool NetworkReporter::queueResult(const ClassificationResult& r) {
#if NET_ACTIVE
  if (!r.success) return false;  // lỗi hệ thống không phải là kết quả
  const char* quality = r.predictedClass == POTATO_GOOD       ? "good"
                        : r.predictedClass == POTATO_NOT_GOOD ? "not_good"
                                                              : "uncertain";
  float confidence = r.confidence;
  if (!(confidence >= 0.0f)) confidence = 0.0f;
  if (confidence > 1.0f) confidence = 1.0f;

  char ts[40] = "";
  const time_t t = time(nullptr);
  if (t > 1700000000) {  // đã đồng bộ NTP
    struct tm tmUtc;
    gmtime_r(&t, &tmUtc);
    strftime(ts, sizeof(ts), ",\"ts\":\"%Y-%m-%dT%H:%M:%SZ\"", &tmUtc);
  }

  if (gCount == NET_QUEUE_LENGTH) {
    if (gInflightMsgId >= 0) {  // phần tử cũ nhất đang được gửi → bỏ bản mới
      ++gDropped;
      Serial.println("[NET] WARNING: queue full, result not queued");
      return false;
    }
    popQueue();
    ++gDropped;
    Serial.println("[NET] WARNING: queue full, oldest unsent result dropped");
  }
  const int slot = (gHead + gCount) % NET_QUEUE_LENGTH;
  const int n = snprintf(gQueue[slot], kMsgLen,
                         "{\"device\":\"%s\",\"quality\":\"%s\",\"confidence\":%.4f,\"seq\":%lu,\"uptime_ms\":%lu%s,"
                         "\"p_good\":%.4f,\"p_not_good\":%.4f,\"raw\":[%d,%d],\"capture_ms\":%.1f,\"pre_ms\":%.1f,"
                         "\"inf_ms\":%.1f,\"fw\":\"%s\"}",
                         POTATO_DEVICE_ID, quality, confidence, (unsigned long)r.sequence, (unsigned long)millis(), ts,
                         r.goodProbability, r.notGoodProbability, r.rawGood, r.rawNotGood,
                         r.captureTimeUs / 1000.0f, r.preprocessTimeUs / 1000.0f, r.inferenceTimeUs / 1000.0f,
                         FW_VERSION);
  if (n <= 0 || n >= static_cast<int>(kMsgLen)) return false;
  gQueueBytes[slot] = static_cast<uint16_t>(n);
  ++gCount;
  Serial.printf("[NET] Result queued for MQTT (%d waiting%s)\n", gCount, gMqttConnected ? "" : ", offline");
  return true;
#else
  (void)r;
  return false;
#endif
}

void NetworkReporter::printStatus() {
#if NET_ACTIVE
  Serial.printf("Network: Wi-Fi %s", WiFi.status() == WL_CONNECTED ? "connected" : "DISCONNECTED");
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf(" (IP %s, RSSI %d dBm)", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
  Serial.printf(", MQTT %s -> %s\n", gMqttConnected ? "connected" : "DISCONNECTED", gUri);
  Serial.printf("Network: device %s, queued %d, sent %lu, dropped %lu, MQTT connects %lu, MQTT errors %lu, "
                "Wi-Fi retries %lu, time %s\n",
                POTATO_DEVICE_ID, gCount, (unsigned long)gSent, (unsigned long)gDropped,
                (unsigned long)gMqttConnects.load(), (unsigned long)gMqttErrors.load(),
                (unsigned long)gWifiReconnects, time(nullptr) > 1700000000 ? "NTP synced" : "not synced");
#else
  Serial.println(ENABLE_NETWORK ? "Network: off (no secrets.h)" : "Network: off (ENABLE_NETWORK = 0)");
#endif
}
