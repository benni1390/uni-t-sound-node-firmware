#include "server_stream.h"

#include <Arduino.h>
#include <WebSocketsClient.h>
#include <WiFi.h>

#include <cstdio>
#include <ctime>

#include "audio.h"
#include "ca_cert.h"
#include "config.h"

namespace {

constexpr size_t kAudioSendChunkBytes = 1024;
constexpr uint32_t kWsReconnectIntervalMs = 5000;

WebSocketsClient ws_client;
char ws_path[160] = {};
char ws_headers[160] = {};
bool ws_started = false;
volatile bool connected = false;
volatile uint32_t audio_bytes_sent = 0;

void on_ws_event(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      audio_discard();
      audio_set_streaming(true);
      connected = true;
      Serial.println("Server: streaming connected");
      break;
    case WStype_DISCONNECTED:
      if (connected) {
        Serial.println("Server: disconnected, audio is dropped");
      }
      audio_set_streaming(false);
      connected = false;
      break;
    case WStype_ERROR:
      Serial.printf("Server: error %.*s\n", static_cast<int>(length),
                    reinterpret_cast<const char *>(payload));
      break;
    default:
      break;
  }
}

void start_stream() {
  if (ws_started || WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (SERVER_TLS && !clock_is_set()) {
    return;
  }
  ws_started = true;

  String node = NODE_ID;
  if (node.length() == 0) {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.toLowerCase();
    node = "esp32-" + mac;
  }
  snprintf(ws_path, sizeof(ws_path),
           "/api/v1/nodes/%s/stream?rate=%lu&threshold=%.1f", node.c_str(),
           static_cast<unsigned long>(kAudioSampleRate),
           static_cast<double>(SOUND_THRESHOLD_DB));
  snprintf(ws_headers, sizeof(ws_headers), "Authorization: Bearer %s",
           SERVER_TOKEN);

  if (SERVER_TLS) {
    ws_client.beginSslWithCA(SERVER_HOST, SERVER_PORT, ws_path, kServerCaCert);
  } else {
    ws_client.begin(SERVER_HOST, SERVER_PORT, ws_path);
  }
  ws_client.setExtraHeaders(ws_headers);
  ws_client.onEvent(on_ws_event);
  ws_client.setReconnectInterval(kWsReconnectIntervalMs);
  ws_client.enableHeartbeat(15000, 3000, 2);
  Serial.printf("Server: connecting to %s://%s:%d%s as node %s (local IP %s)\n",
                SERVER_TLS ? "wss" : "ws", SERVER_HOST,
                static_cast<int>(SERVER_PORT), ws_path, node.c_str(),
                WiFi.localIP().toString().c_str());
}

void forward_audio_to_server() {
  static uint8_t chunk[kAudioSendChunkBytes];
  if (!connected) {
    audio_discard();
    return;
  }
  for (int i = 0; i < 4; ++i) {
    const size_t length =
        audio_read(chunk, sizeof(chunk));
    if (length == 0) {
      break;
    }
    if (!ws_client.sendBIN(chunk, length)) {
      break;
    }
    audio_bytes_sent += length;
  }
}

}  // namespace

// Certificate validation needs a valid clock, so TLS waits for NTP.
bool clock_is_set() {
  static bool ntp_started = false;
  if (!ntp_started) {
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
    ntp_started = true;
  }
  return time(nullptr) > 1700000000;
}

void server_start() { start_stream(); }

void server_loop() {
  ws_client.loop();
  forward_audio_to_server();
}

bool server_connected() { return connected; }

void server_send_reading(float decibels) {
  if (!connected) {
    return;
  }
  char message[32];
  snprintf(message, sizeof(message), "{\"db\":%.1f}",
           static_cast<double>(decibels));
  ws_client.sendTXT(message);
}

uint32_t server_audio_bytes_sent() { return audio_bytes_sent; }
