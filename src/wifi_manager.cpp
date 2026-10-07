#include "wifi_manager.h"

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"

void ensure_wifi() {
  static bool started = false;
  if (WiFi.status() == WL_CONNECTED || started) {
    return;
  }
  started = true;
  WiFi.mode(WIFI_STA);
  WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        Serial.printf("WiFi: connected, IP %s, gateway %s, RSSI %d dBm\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.gatewayIP().toString().c_str(), WiFi.RSSI());
        break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        Serial.printf("WiFi: disconnected, reason %d\n",
                      info.wifi_sta_disconnected.reason);
        break;
      default:
        break;
    }
  });
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("WiFi: connecting to %s\n", WIFI_SSID);
}
