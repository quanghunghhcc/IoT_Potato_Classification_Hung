// Host stub of ESP-IDF esp-mqtt (IDF 5.x structure layout) used by network_reporter.cpp (syntax check only).
#pragma once
#include <cstdint>
#include "esp_camera.h"   // esp_err_t, ESP_OK
typedef const char* esp_event_base_t;
typedef void (*esp_event_handler_t)(void*, esp_event_base_t, int32_t, void*);
typedef struct esp_mqtt_client* esp_mqtt_client_handle_t;
typedef enum { MQTT_EVENT_ANY = -1, MQTT_EVENT_ERROR = 0, MQTT_EVENT_CONNECTED, MQTT_EVENT_DISCONNECTED, MQTT_EVENT_SUBSCRIBED,
  MQTT_EVENT_UNSUBSCRIBED, MQTT_EVENT_PUBLISHED, MQTT_EVENT_DATA, MQTT_EVENT_BEFORE_CONNECT, MQTT_EVENT_DELETED } esp_mqtt_event_id_t;
typedef struct esp_mqtt_event_t { esp_mqtt_event_id_t event_id; esp_mqtt_client_handle_t client; int msg_id; } esp_mqtt_event_t;
typedef esp_mqtt_event_t* esp_mqtt_event_handle_t;
typedef struct esp_mqtt_client_config_t {
  struct { struct { const char* uri; const char* hostname; uint32_t port; } address; } broker;
  struct { const char* username; const char* client_id; struct { const char* password; } authentication; } credentials;
  struct { struct { const char* topic; const char* msg; int msg_len; int qos; int retain; } last_will; int keepalive; } session;
  struct { int reconnect_timeout_ms; int timeout_ms; } network;
} esp_mqtt_client_config_t;
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t*);
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t, esp_mqtt_event_id_t, esp_event_handler_t, void*);
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t);
int esp_mqtt_client_publish(esp_mqtt_client_handle_t, const char*, const char*, int, int, int);
int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t, const char*, const char*, int, int, int, bool);
