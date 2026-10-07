#include "status.h"

#include <Arduino.h>
#include <WiFi.h>

#include "audio.h"
#include "meter_ble.h"
#include "server_stream.h"

void print_status() {
  static uint32_t last_ms = 0;
  static uint32_t last_notifications = 0;
  static uint32_t last_bytes = 0;
  const uint32_t now = millis();
  if (now - last_ms < 5000) {
    return;
  }
  const uint32_t seconds = (now - last_ms) / 1000;
  last_ms = now;

  const uint32_t notifications = meter_notifications();
  const uint32_t bytes = server_audio_bytes_sent();
  const bool wifi_up = WiFi.status() == WL_CONNECTED;
  const bool meter_up = meter_connected();

  Serial.printf("Status: wifi=%s ip=%s server=%s meter=%s audio=%s\n",
                wifi_up ? "up" : "down",
                wifi_up ? WiFi.localIP().toString().c_str() : "-",
                server_connected() ? "up" : "down", meter_up ? "up" : "down",
                audio_ready() ? "ok" : "off");
  if (meter_up) {
    Serial.printf("  BLE: %lu readings/s, last %.1f dBA, parse errors %lu, "
                  "RSSI %d dBm\n",
                  static_cast<unsigned long>((notifications -
                                              last_notifications) /
                                             seconds),
                  static_cast<double>(meter_last_db()),
                  static_cast<unsigned long>(meter_parse_errors()),
                  meter_rssi());
  }
  {
    // A floating/missing mic reads as constant 0 or a stuck value, so the
    // peak never varies between chunks; a live mic always has noise.
    uint16_t peak = 0;
    uint16_t low = 0;
    audio_take_peaks(peak, low);
    const char *verdict = !audio_mic_present() ? "NOT STREAMING (no mic signal)"
                          : peak < 8 ? "NO SIGNAL (mic missing/wired wrong?)"
                          : (peak == low ? "constant value (mic not driving data?)"
                                         : "signal present");
    Serial.printf("  Mic: peak %u, min chunk peak %u -> %s\n", peak, low, verdict);
  }
  if (server_connected()) {
    Serial.printf("  Audio: %lu kbit/s sent, buffer %u/%u bytes\n",
                  static_cast<unsigned long>((bytes - last_bytes) * 8 /
                                             seconds / 1000),
                  static_cast<unsigned>(audio_buffered_bytes()),
                  static_cast<unsigned>(kAudioStreamBufferBytes));
  }
  Serial.printf("  Heap: %lu free, min %lu\n",
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getMinFreeHeap()));
  last_notifications = notifications;
  last_bytes = bytes;
}
