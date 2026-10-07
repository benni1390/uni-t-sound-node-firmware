#include <Arduino.h>
#include <WiFi.h>

#include "audio.h"
#include "config.h"
#include "measurement.h"
#include "meter_ble.h"
#include "mqtt_publisher.h"
#include "server_stream.h"
#include "status.h"
#include "wifi_manager.h"

namespace {

bool is_configured() {
  return WIFI_SSID[0] != '\0' && SERVER_HOST[0] != '\0' &&
         SERVER_TOKEN[0] != '\0';
}

void publish_pending_measurement() {
  Measurement current{};
  if (!meter_take_measurement(current)) {
    return;
  }
  server_send_reading(current.decibels);
  if (mqtt_enabled()) {
    mqtt_publish_measurement(current);
  }
  Serial.printf("UT353BT: %.1f dBA flags=%s\n", current.decibels,
                current.flags);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);  // lets the serial monitor attach before the banner
  Serial.printf("Reset reason: %d\n", static_cast<int>(esp_reset_reason()));
  Serial.printf("UT353BT node starting; server %s:%d, meter %s\n", SERVER_HOST,
                static_cast<int>(SERVER_PORT),
                METER_MAC[0] != '\0' ? METER_MAC : "(any)");

  if (!is_configured()) {
    Serial.println("Configure WIFI_SSID, SERVER_HOST and SERVER_TOKEN in "
                   "include/secrets.h");
    return;
  }

  mqtt_setup();
  ensure_wifi();
  audio_start();
  meter_start();
}

void loop() {
  if (!is_configured()) {
    delay(1000);
    return;
  }

  ensure_wifi();
  mqtt_loop();
  if (audio_ready()) {
    server_start();
    server_loop();
  }

  publish_pending_measurement();
  print_status();
  delay(5);
}
