#include "mqtt_publisher.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "ca_cert.h"
#include "config.h"
#include "measurement.h"
#include "meter_ble.h"
#include "server_stream.h"

namespace {

constexpr uint32_t kMqttReconnectIntervalMs = 5000;
constexpr uint32_t kDiagnosticPublishIntervalMs = 30000;

WiFiClient network_client;
WiFiClientSecure secure_network_client;
PubSubClient mqtt_client;
uint32_t last_mqtt_attempt_ms = 0;
uint32_t last_diagnostic_publish_ms = 0;
bool mqtt_state_pending = true;
bool last_ble_connected = false;
bool have_ble_connection_state = false;
bool have_published_reading_state = false;
bool have_published_flags = false;
Measurement last_published_reading_state{};
char last_published_flags[32] = {};

void ensure_mqtt() {
  if (MQTT_HOST[0] == '\0' || WiFi.status() != WL_CONNECTED ||
      mqtt_client.connected() || meter_topic_prefix()[0] == '\0') {
    return;
  }
  const uint32_t now = millis();
  if (now - last_mqtt_attempt_ms < kMqttReconnectIntervalMs) {
    return;
  }
  if (MQTT_TLS && !clock_is_set()) {
    return;
  }
  last_mqtt_attempt_ms = now;

  // Unique per device, otherwise nodes would disconnect each other.
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  const String client_id = "ut353bt-" + mac;
  char availability_topic[128];
  snprintf(availability_topic, sizeof(availability_topic), "%savailability",
           meter_topic_prefix());
  const bool connected =
      MQTT_USER[0] == '\0'
          ? mqtt_client.connect(client_id.c_str(), availability_topic, 1, true,
                                "offline")
          : mqtt_client.connect(client_id.c_str(), MQTT_USER, MQTT_PASSWORD,
                                availability_topic, 1, true, "offline");
  if (connected) {
    mqtt_state_pending = true;
    have_published_reading_state = false;
    have_published_flags = false;
    snprintf(availability_topic, sizeof(availability_topic), "%savailability",
             meter_topic_prefix());
    mqtt_client.publish(availability_topic, "online", true);
    Serial.println("MQTT: connected");
  } else {
    Serial.printf("MQTT: connection failed, state=%d\n", mqtt_client.state());
  }
}

void publish_measurement(const Measurement &current) {
  if (mqtt_client.connected() && meter_topic_prefix()[0] != '\0') {
    char topic[128];
    char value[24];
    snprintf(topic, sizeof(topic), "%sdba", meter_topic_prefix());
    snprintf(value, sizeof(value), "%.1f", current.decibels);
    mqtt_client.publish(topic, value);

    if (!have_published_reading_state ||
        current.battery_low != last_published_reading_state.battery_low ||
        current.hold != last_published_reading_state.hold ||
        strcmp(current.mode, last_published_reading_state.mode) != 0 ||
        strcmp(current.speed, last_published_reading_state.speed) != 0) {
      snprintf(topic, sizeof(topic), "%sbattery_low", meter_topic_prefix());
      mqtt_client.publish(topic, current.battery_low ? "1" : "0", true);
      snprintf(topic, sizeof(topic), "%shold", meter_topic_prefix());
      mqtt_client.publish(topic, current.hold ? "1" : "0", true);
      snprintf(topic, sizeof(topic), "%smode", meter_topic_prefix());
      mqtt_client.publish(topic, current.mode, true);
      snprintf(topic, sizeof(topic), "%sspeed", meter_topic_prefix());
      mqtt_client.publish(topic, current.speed, true);
      last_published_reading_state = current;
      have_published_reading_state = true;
    }
    if (!have_published_flags ||
        strcmp(current.flags, last_published_flags) != 0) {
      snprintf(topic, sizeof(topic), "%sflags", meter_topic_prefix());
      mqtt_client.publish(topic, current.flags, true);
      snprintf(last_published_flags, sizeof(last_published_flags), "%s",
               current.flags);
      have_published_flags = true;
    }

    const uint32_t now = millis();
    if (now - last_diagnostic_publish_ms >= kDiagnosticPublishIntervalMs) {
      if (meter_connected()) {
        snprintf(topic, sizeof(topic), "%srssi", meter_topic_prefix());
        snprintf(value, sizeof(value), "%d", meter_rssi());
        mqtt_client.publish(topic, value);
      }
      if (meter_connected() &&
          now - meter_last_measurement_ms() < 5000 && clock_is_set()) {
        snprintf(topic, sizeof(topic), "%slast_seen", meter_topic_prefix());
        snprintf(value, sizeof(value), "%lld",
                 static_cast<long long>(time(nullptr)));
        mqtt_client.publish(topic, value, true);
      }
      last_diagnostic_publish_ms = now;
    }
  }

}

void publish_ble_connection_state() {
  if (!mqtt_client.connected() || meter_topic_prefix()[0] == '\0') {
    mqtt_state_pending = true;
    return;
  }

  const bool connected = meter_connected();
  if (!mqtt_state_pending && have_ble_connection_state &&
      connected == last_ble_connected) {
    return;
  }

  char topic[128];
  snprintf(topic, sizeof(topic), "%sconnected", meter_topic_prefix());
  mqtt_client.publish(topic, connected ? "1" : "0", true);
  last_ble_connected = connected;
  have_ble_connection_state = true;
  mqtt_state_pending = false;
}

}  // namespace

bool mqtt_enabled() { return MQTT_HOST[0] != '\0'; }

void mqtt_setup() {
  if (!mqtt_enabled()) {
    return;
  }
  if (MQTT_TLS) {
    secure_network_client.setCACert(kServerCaCert);
    mqtt_client.setClient(secure_network_client);
  } else {
    mqtt_client.setClient(network_client);
  }
  mqtt_client.setServer(MQTT_HOST, MQTT_PORT);
  mqtt_client.setBufferSize(256);
}

void mqtt_loop() {
  ensure_mqtt();
  if (mqtt_enabled()) {
    mqtt_client.loop();
    publish_ble_connection_state();
  }
}

void mqtt_publish_connection_state() { publish_ble_connection_state(); }

void mqtt_publish_measurement(const Measurement &current) {
  publish_measurement(current);
}
