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
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>

#include "esp_camera.h"
#include "img_converters.h"
#include "mqtt_client.h"  // ESP-MQTT của ESP-IDF, có sẵn trong Arduino-ESP32 core 3.x
#else
#define NET_ACTIVE 0
#endif

#if NET_ACTIVE && defined(POTATO_IMAGE_UPLOAD_TOKEN)
#define NET_IMAGE_UPLOAD 1
#else
#define NET_IMAGE_UPLOAD 0
#endif

#ifndef POTATO_DEVICE_ID
#define POTATO_DEVICE_ID "potato-01"
#endif
#ifndef POTATO_MQTT_PORT
#define POTATO_MQTT_PORT 1883
#endif
#ifndef POTATO_BACKEND_PORT
#define POTATO_BACKEND_PORT 8000
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

#if NET_IMAGE_UPLOAD
std::atomic<bool> gImageUploadPending{false};

struct ImageUploadJob {
  uint8_t* jpeg;
  size_t length;
  uint32_t sequence;
  uint32_t uptimeMs;
};

void imageUploadTask(void* argument) {
  ImageUploadJob* job = static_cast<ImageUploadJob*>(argument);
  char url[112];
  snprintf(url, sizeof(url), "http://%s:%d/api/images", POTATO_MQTT_HOST, (int)POTATO_BACKEND_PORT);
  char sequenceText[16], uptimeText[16];
  snprintf(sequenceText, sizeof(sequenceText), "%lu", (unsigned long)job->sequence);
  snprintf(uptimeText, sizeof(uptimeText), "%lu", (unsigned long)job->uptimeMs);
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(2000);
  http.setTimeout(3000);
  int status = HTTPC_ERROR_CONNECTION_REFUSED;
  if (WiFi.status() == WL_CONNECTED && http.begin(client, url)) {
    http.addHeader("Content-Type", "image/jpeg");
    http.addHeader("X-Upload-Token", POTATO_IMAGE_UPLOAD_TOKEN);
    http.addHeader("X-Device-ID", POTATO_DEVICE_ID);
    http.addHeader("X-Sequence", sequenceText);
    http.addHeader("X-Uptime-Ms", uptimeText);
    status = http.POST(job->jpeg, job->length);
    http.end();
  }
  if (status == 201) Serial.printf("[NET] Image uploaded for seq=%lu\n", (unsigned long)job->sequence);
  else Serial.printf("[NET] Image upload failed (HTTP %d)\n", status);
  free(job->jpeg);
  free(job);
  gImageUploadPending.store(false);
  vTaskDelete(nullptr);
}
#endif

void mqttEventHandler(void*, esp_event_base_t, int32_t eventId, void* eventData) {
  const esp_mqtt_event_handle_t ev = static_cast<esp_mqtt_event_handle_t>(eventData);
  switch (eventId) {
    case MQTT_EVENT_CONNECTED:
      gMqttConnected.store(true);
      gMqttConnects.fetch_add(1);
      break;
    case MQTT_EVENT_DISCONNECTED:
      gMqttConnected.store(false);
      break;
    case MQTT_EVENT_PUBLISHED:
      gAckedMsgId.store(ev->msg_id);
      break;
    case MQTT_EVENT_ERROR:
      gMqttErrors.fetch_add(1);
      break;
    default:
      break;
  }
}

void popQueue() {
  if (gCount == 0) return;
  gHead = (gHead + 1) % NET_QUEUE_LENGTH;
  --gCount;
}

}  // namespace
#endif

void NetworkReporter::begin() {
#if NET_ACTIVE
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(POTATO_WIFI_SSID, POTATO_WIFI_PASSWORD);
  gWifiLastBeginMs = millis();
  Serial.printf("[NET] Wi-Fi starting (SSID: %s, device: %s)\n", POTATO_WIFI_SSID, POTATO_DEVICE_ID);
#if !NET_IMAGE_UPLOAD
  Serial.println("[NET] Photo upload disabled: add POTATO_IMAGE_UPLOAD_TOKEN to secrets.h");
#endif
  snprintf(gStatusTopic, sizeof(gStatusTopic), "%s/%s/status", NET_TOPIC_PREFIX, POTATO_DEVICE_ID);
  snprintf(gResultTopic, sizeof(gResultTopic), "%s/%s/result", NET_TOPIC_PREFIX, POTATO_DEVICE_ID);
  snprintf(gUri, sizeof(gUri), "mqtt://%s:%d", POTATO_MQTT_HOST, POTATO_MQTT_PORT);
#else
  Serial.println(ENABLE_NETWORK ? "[NET] Network disabled: no secrets.h" : "[NET] Network disabled by config");
#endif
}

bool NetworkReporter::enabled() const {
#if NET_ACTIVE
  return true;
#else
  return false;
#endif
}

bool NetworkReporter::wifiConnected() const {
#if NET_ACTIVE
  return WiFi.status() == WL_CONNECTED;
#else
  return false;
#endif
}

bool NetworkReporter::mqttConnected() const {
#if NET_ACTIVE
  return gMqttConnected.load();
#else
  return false;
#endif
}

const char* NetworkReporter::deviceId() const {
#if NET_ACTIVE
  return POTATO_DEVICE_ID;
#else
  return "disabled";
#endif
}

void NetworkReporter::loop() {
#if NET_ACTIVE
  const uint32_t now = millis();
  const bool wifiUp = (WiFi.status() == WL_CONNECTED);

  if (wifiUp && !gWifiWasConnected) {
    gWifiWasConnected = true;
    Serial.printf("[NET] Wi-Fi connected, IP %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
    if (!gNtpStarted) {
      configTime(0, 0, "pool.ntp.org", "time.google.com", nullptr);
      gNtpStarted = true;
    }
  } else if (!wifiUp && gWifiWasConnected) {
    gWifiWasConnected = false;
    Serial.println("[NET] Wi-Fi lost");
  }

  if (!wifiUp && (now - gWifiLastBeginMs > 30000)) {
    gWifiLastBeginMs = now;
    ++gWifiReconnects;
    WiFi.disconnect();
    WiFi.begin(POTATO_WIFI_SSID, POTATO_WIFI_PASSWORD);
  }

  if (wifiUp && !gMqttStarted) {
    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = gUri;
    cfg.credentials.client_id = POTATO_DEVICE_ID;
#ifdef POTATO_MQTT_USERNAME
    if (strlen(POTATO_MQTT_USERNAME) > 0) cfg.credentials.username = POTATO_MQTT_USERNAME;
#endif
#ifdef POTATO_MQTT_PASSWORD
    if (strlen(POTATO_MQTT_PASSWORD) > 0) cfg.credentials.authentication.password = POTATO_MQTT_PASSWORD;
#endif
    cfg.session.keepalive = 30;
    cfg.session.last_will.topic = gStatusTopic;
    cfg.session.last_will.msg = "offline";
    cfg.session.last_will.msg_len = 7;
    cfg.session.last_will.qos = 1;
    cfg.session.last_will.retain = 1;
    cfg.network.reconnect_timeout_ms = 5000;

    gClient = esp_mqtt_client_init(&cfg);
    if (gClient != nullptr) {
      esp_mqtt_client_register_event(gClient, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),
                                     mqttEventHandler, nullptr);
      esp_mqtt_client_start(gClient);
      gMqttStarted = true;
      Serial.printf("[NET] ESP-MQTT client started -> %s\n", gUri);
    } else {
      Serial.println("[NET] ERROR: esp_mqtt_client_init failed");
    }
  }

  const bool mqttUp = gMqttConnected.load();
  if (mqttUp != gLastMqttState) {
    gLastMqttState = mqttUp;
    if (mqttUp) {
      Serial.println("[NET] MQTT broker connected");
      esp_mqtt_client_publish(gClient, gStatusTopic, "online", 6, 1, 1);
    } else {
      Serial.println("[NET] MQTT broker disconnected");
      gInflightMsgId = -1;
    }
  }

  const int acked = gAckedMsgId.exchange(-1);
  if (acked >= 0 && acked == gInflightMsgId) {
    gInflightMsgId = -1;
    popQueue();
    ++gSent;
  } else if (gInflightMsgId >= 0) {
    if (!mqttUp || (now - gInflightSinceMs > 8000)) {
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

bool NetworkReporter::queueResult(const ClassificationResult& r, uint32_t uptimeMs) {
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
                         POTATO_DEVICE_ID, quality, confidence, (unsigned long)r.sequence, (unsigned long)uptimeMs, ts,
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
  (void)uptimeMs;
  return false;
#endif
}

bool NetworkReporter::uploadImage(const camera_fb_t* frame, uint32_t sequence, uint32_t uptimeMs) {
#if NET_IMAGE_UPLOAD
  if (frame == nullptr || WiFi.status() != WL_CONNECTED) return false;
  bool expected = false;
  if (!gImageUploadPending.compare_exchange_strong(expected, true)) {
    Serial.println("[NET] Previous photo is still uploading; skipping this image");
    return false;
  }
  uint8_t* jpeg = nullptr;
  size_t jpegLength = 0;
  const bool allocated = frame->format != PIXFORMAT_JPEG;
  if (allocated) {
    if (!fmt2jpg(frame->buf, frame->len, frame->width, frame->height, frame->format, 80, &jpeg, &jpegLength)) {
      Serial.println("[NET] JPEG compression failed; image not uploaded");
      gImageUploadPending.store(false);
      return false;
    }
  } else {
    jpeg = static_cast<uint8_t*>(malloc(frame->len));
    if (jpeg == nullptr) {
      gImageUploadPending.store(false);
      return false;
    }
    memcpy(jpeg, frame->buf, frame->len);
    jpegLength = frame->len;
  }
  ImageUploadJob* job = static_cast<ImageUploadJob*>(malloc(sizeof(ImageUploadJob)));
  if (job == nullptr) {
    free(jpeg);
    gImageUploadPending.store(false);
    return false;
  }
  *job = {jpeg, jpegLength, sequence, uptimeMs};
  if (xTaskCreate(imageUploadTask, "photo-upload", 8192, job, 1, nullptr) != pdPASS) {
    free(jpeg);
    free(job);
    gImageUploadPending.store(false);
    Serial.println("[NET] Could not start photo upload task");
    return false;
  }
  Serial.printf("[NET] Photo upload queued for seq=%lu\n", (unsigned long)sequence);
  return true;
#else
  (void)frame;
  (void)sequence;
  (void)uptimeMs;
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
