#include <Arduino.h>
#include <NimBLEDevice.h>
#include <PubSubClient.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#include "ca_cert.h"

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#ifndef SERVER_HOST
#define SERVER_HOST ""
#endif
#ifndef SERVER_PORT
#define SERVER_PORT 8080
#endif
#ifndef SERVER_TLS
#define SERVER_TLS false
#endif
#ifndef SERVER_TOKEN
#define SERVER_TOKEN ""
#endif
#ifndef NODE_ID
#define NODE_ID ""
#endif
#ifndef MQTT_HOST
#define MQTT_HOST ""
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif
#ifndef MQTT_TLS
#define MQTT_TLS false
#endif
#ifndef MQTT_USER
#define MQTT_USER ""
#endif
#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD ""
#endif
#ifndef MQTT_BASE_TOPIC
#define MQTT_BASE_TOPIC "ut353bt"
#endif
#ifndef METER_MAC
#define METER_MAC ""
#endif
#ifndef SOUND_THRESHOLD_DB
#define SOUND_THRESHOLD_DB 60.0f
#endif
#ifndef MIC_I2S_BCLK
#define MIC_I2S_BCLK 2
#endif
#ifndef MIC_I2S_WS
#define MIC_I2S_WS 3
#endif
#ifndef MIC_I2S_DATA
#define MIC_I2S_DATA 10
#endif

namespace {

constexpr char kDeviceName[] = "UT353BT";
constexpr char kServiceUuid[] = "0000ff12-0000-1000-8000-00805f9b34fb";
constexpr char kDataInUuid[] = "0000ff01-0000-1000-8000-00805f9b34fb";
constexpr char kDataOutUuid[] = "0000ff02-0000-1000-8000-00805f9b34fb";
constexpr uint32_t kScanIntervalMs = 5000;
constexpr uint32_t kPollIntervalMs = 125;
constexpr uint32_t kMqttReconnectIntervalMs = 5000;
constexpr uint32_t kDiagnosticPublishIntervalMs = 30000;
constexpr uint32_t kAudioSampleRate = 16000;
constexpr size_t kAudioSamplesPerRead = 256;
constexpr size_t kAudioStreamBufferBytes = 16384;
constexpr size_t kAudioSendChunkBytes = 1024;
// A connected mic always shows some noise; a missing one reads as a constant.
constexpr int32_t kMicMinSpan = 4;
constexpr uint32_t kMicHoldMs = 3000;
constexpr uint32_t kWsReconnectIntervalMs = 5000;

WiFiClient network_client;
WiFiClientSecure secure_network_client;
PubSubClient mqtt_client;
WebSocketsClient ws_client;
StreamBufferHandle_t audio_stream = nullptr;
i2s_chan_handle_t rx_channel = nullptr;
NimBLEClient *ble_client = nullptr;
NimBLERemoteCharacteristic *data_in = nullptr;
portMUX_TYPE measurement_mux = portMUX_INITIALIZER_UNLOCKED;

struct Measurement {
  float decibels;
  bool hold;
  bool battery_low;
  char mode[8];
  char speed[8];
  char flags[32];
};

Measurement pending_measurement{};
bool measurement_pending = false;
char mqtt_topic_prefix[96] = {};
char ws_path[160] = {};
char ws_headers[160] = {};
uint32_t last_scan_ms = 0;
uint32_t last_poll_ms = 0;
uint32_t last_mqtt_attempt_ms = 0;
uint32_t last_diagnostic_publish_ms = 0;
volatile bool audio_ready = false;
volatile bool server_connected = false;
volatile uint32_t ble_notifications = 0;
volatile uint32_t ble_parse_errors = 0;
volatile uint32_t last_measurement_ms = 0;
volatile uint32_t audio_bytes_sent = 0;
volatile bool mic_present = false;
volatile uint32_t mic_live_until_ms = 0;
volatile uint16_t audio_peak = 0;
volatile uint16_t audio_min_peak = 0xFFFF;
volatile float last_db = -1.0f;
uint32_t ble_poll_failures = 0;
bool ws_started = false;
bool mqtt_state_pending = true;
bool last_ble_connected = false;
bool have_ble_connection_state = false;
bool have_published_reading_state = false;
bool have_published_flags = false;
Measurement last_published_reading_state{};
char last_published_flags[32] = {};

bool parse_measurement(const uint8_t *data, size_t length, Measurement &out) {
  if (length < 19 || data[0] != 0xAA || data[1] != 0xBB ||
      data[4] != ';' || data[14] != '=') {
    return false;
  }

  char display[10] = {};
  memcpy(display, data + 5, 9);
  char *unit = strstr(display, "dBA");
  if (unit == nullptr) {
    return false;
  }
  *unit = '\0';

  char *number = display;
  while (*number == ' ') {
    ++number;
  }
  if (*number == '\0') {
    return false;
  }

  char *end = nullptr;
  const float decibels = strtof(number, &end);
  if (end == number || *end != '\0' || decibels < 0.0f || decibels > 200.0f) {
    return false;
  }

  const uint32_t status = static_cast<uint32_t>(data[15]) << 24 |
                          static_cast<uint32_t>(data[16]) << 16 |
                          static_cast<uint32_t>(data[17]) << 8 |
                          static_cast<uint32_t>(data[18]);
  const uint8_t speed = (status >> 24) & 0x07;
  const uint8_t mode = (status >> 18) & 0x03;

  out.decibels = decibels;
  out.hold = ((status >> 16) & 0x01) != 0;
  out.battery_low = ((status >> 22) & 0x01) != 0 ||
                    ((status >> 8) & 0x01) != 0;
  const char *mode_name = mode == 0x02 ? "max" : mode == 0x01 ? "min"
                                     : mode == 0x00 ? "normal" : "unknown";
  const char *speed_name = speed == 0x04 ? "fast" : speed == 0x03 ? "slow"
                                       : "unknown";
  snprintf(out.mode, sizeof(out.mode), "%s", mode_name);
  snprintf(out.speed, sizeof(out.speed), "%s", speed_name);

  size_t used = 0;
  auto append_flag = [&out, &used](const char *flag) {
    const int written = snprintf(out.flags + used, sizeof(out.flags) - used,
                                 "%s%s", used == 0 ? "" : ",", flag);
    if (written > 0 && static_cast<size_t>(written) < sizeof(out.flags) - used) {
      used += static_cast<size_t>(written);
    }
  };
  if (out.hold) append_flag("hold");
  if ((data[16] & 0x02) != 0) append_flag("unk");
  if (mode == 0x01) append_flag("min");
  if (mode == 0x02) append_flag("max");
  if (speed == 0x04) append_flag("fast");

  return true;
}

void on_notification(NimBLERemoteCharacteristic *, uint8_t *data, size_t length,
                     bool) {
  Measurement parsed{};
  if (!parse_measurement(data, length, parsed)) {
    ++ble_parse_errors;
    if (ble_parse_errors <= 5) {
      Serial.printf("BLE: unparsable notification (%u bytes):", length);
      for (size_t i = 0; i < length && i < 24; ++i) {
        Serial.printf(" %02x", data[i]);
      }
      Serial.println();
    }
    return;
  }
  ++ble_notifications;
  last_measurement_ms = millis();
  last_db = parsed.decibels;
  if (ble_notifications == 1) {
    Serial.println("BLE: first valid measurement received");
  }

  portENTER_CRITICAL(&measurement_mux);
  pending_measurement = parsed;
  measurement_pending = true;
  portEXIT_CRITICAL(&measurement_mux);
}


// Audio is only streamed while the server connection is up; with no
// connection samples are dropped, nothing is stored on the node.
void record_audio_samples() {
  int32_t input[kAudioSamplesPerRead];
  int16_t output[kAudioSamplesPerRead];
  size_t bytes_read = 0;
  const esp_err_t result =
      i2s_channel_read(rx_channel, input, sizeof(input), &bytes_read, 100);
  if (result != ESP_OK || bytes_read == 0) {
    vTaskDelay(pdMS_TO_TICKS(10));  // never spin; keeps the watchdog fed
    return;
  }

  const size_t sample_count = bytes_read / sizeof(input[0]);
  int32_t chunk_peak = 0;
  int32_t lo = INT32_MAX;
  int32_t hi = INT32_MIN;
  for (size_t i = 0; i < sample_count; ++i) {
    output[i] = static_cast<int16_t>(input[i] >> 16);
    const int32_t sample = output[i];
    const int32_t v = sample < 0 ? -sample : sample;
    if (v > chunk_peak) chunk_peak = v;
    if (sample < lo) lo = sample;
    if (sample > hi) hi = sample;
  }
  if (hi - lo >= kMicMinSpan) {
    mic_live_until_ms = millis() + kMicHoldMs;
  }
  mic_present = static_cast<int32_t>(mic_live_until_ms - millis()) > 0;
  if (chunk_peak > audio_peak) audio_peak = chunk_peak;
  if (chunk_peak < audio_min_peak) audio_min_peak = chunk_peak;
  if (!server_connected || !mic_present) {
    return;
  }
  xStreamBufferSend(audio_stream, output, sample_count * sizeof(output[0]), 0);
}

void audio_task(void *) {
  for (;;) {
    record_audio_samples();
  }
}

bool setup_audio() {
  audio_stream = xStreamBufferCreate(kAudioStreamBufferBytes, 1);
  if (audio_stream == nullptr) {
    Serial.println("Audio: cannot allocate stream buffer");
    return false;
  }

  i2s_chan_config_t channel_config =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  channel_config.dma_desc_num = 4;
  channel_config.dma_frame_num = kAudioSamplesPerRead;
  if (i2s_new_channel(&channel_config, nullptr, &rx_channel) != ESP_OK) {
    Serial.println("Audio: I2S channel allocation failed");
    return false;
  }

  i2s_std_config_t std_config = {};
  std_config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kAudioSampleRate);
  std_config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
      I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO);
  std_config.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
  std_config.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  std_config.gpio_cfg.bclk = static_cast<gpio_num_t>(MIC_I2S_BCLK);
  std_config.gpio_cfg.ws = static_cast<gpio_num_t>(MIC_I2S_WS);
  std_config.gpio_cfg.dout = I2S_GPIO_UNUSED;
  std_config.gpio_cfg.din = static_cast<gpio_num_t>(MIC_I2S_DATA);

  if (i2s_channel_init_std_mode(rx_channel, &std_config) != ESP_OK ||
      i2s_channel_enable(rx_channel) != ESP_OK) {
    Serial.println("Audio: I2S initialization failed");
    i2s_del_channel(rx_channel);
    rx_channel = nullptr;
    return false;
  }
  audio_ready = true;
  Serial.printf("Audio: %lu Hz mono, streamed to server\n",
                static_cast<unsigned long>(kAudioSampleRate));
  return true;
}

void on_ws_event(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      xStreamBufferReset(audio_stream);
      server_connected = true;
      Serial.println("Server: streaming connected");
      break;
    case WStype_DISCONNECTED:
      if (server_connected) {
        Serial.println("Server: disconnected, audio is dropped");
      }
      server_connected = false;
      break;
    case WStype_ERROR:
      Serial.printf("Server: error %.*s\n", static_cast<int>(length),
                    reinterpret_cast<const char *>(payload));
      break;
    default:
      break;
  }
}

// Certificate validation needs a valid clock, so TLS waits for NTP.
bool clock_is_set() {
  static bool ntp_started = false;
  if (!ntp_started) {
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
    ntp_started = true;
  }
  return time(nullptr) > 1700000000;
}

void start_server_stream() {
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
  if (!server_connected) {
    xStreamBufferReset(audio_stream);
    return;
  }
  for (int i = 0; i < 4; ++i) {
    const size_t length =
        xStreamBufferReceive(audio_stream, chunk, sizeof(chunk), 0);
    if (length == 0) {
      break;
    }
    if (!ws_client.sendBIN(chunk, length)) {
      break;
    }
    audio_bytes_sent += length;
  }
}

bool connect_to_meter() {
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);

  Serial.println("BLE: scanning for UT353BT...");
  NimBLEScanResults results = scan->getResults(3000, false);
  const NimBLEAdvertisedDevice *meter = nullptr;
  for (int i = 0; i < results.getCount(); ++i) {
    const NimBLEAdvertisedDevice *candidate = results.getDevice(i);
    const bool matching_name =
        candidate != nullptr && candidate->haveName() &&
        strncasecmp(candidate->getName().c_str(), kDeviceName, 5) == 0;
    const bool matching_address =
        candidate != nullptr && METER_MAC[0] != '\0' &&
        strcasecmp(candidate->getAddress().toString().c_str(), METER_MAC) == 0;
    if (matching_name &&
        (METER_MAC[0] == '\0' || matching_address)) {
      meter = candidate;
      break;
    }
  }
  if (meter == nullptr) {
    Serial.printf("BLE: UT353BT not found; %d other devices seen\n",
                  results.getCount());
    for (int i = 0; i < results.getCount() && i < 8; ++i) {
      const NimBLEAdvertisedDevice *seen = results.getDevice(i);
      Serial.printf("  %s  %d dBm  \"%s\"\n",
                    seen->getAddress().toString().c_str(), seen->getRSSI(),
                    seen->haveName() ? seen->getName().c_str() : "");
    }
    scan->clearResults();
    return false;
  }

  char address_buffer[18];
  snprintf(address_buffer, sizeof(address_buffer), "%s",
           meter->getAddress().toString().c_str());
  Serial.printf("BLE: found %s\n", address_buffer);
  if (ble_client == nullptr) {
    ble_client = NimBLEDevice::createClient();
    ble_client->setConnectTimeout(10000);
  }
  if (!ble_client->connect(meter)) {
    Serial.println("BLE: connection failed");
    scan->clearResults();
    return false;
  }
  scan->clearResults();

  NimBLERemoteService *service = ble_client->getService(kServiceUuid);
  if (service == nullptr) {
    Serial.println("BLE: service ff12 not found");
    ble_client->disconnect();
    return false;
  }

  data_in = service->getCharacteristic(kDataInUuid);
  NimBLERemoteCharacteristic *data_out =
      service->getCharacteristic(kDataOutUuid);
  if (data_in == nullptr || data_out == nullptr) {
    Serial.println("BLE: required characteristics ff01/ff02 not found");
    ble_client->disconnect();
    data_in = nullptr;
    return false;
  }
  if (!data_out->canNotify() ||
      !data_out->subscribe(true, on_notification, false)) {
    Serial.println("BLE: could not subscribe to ff02 notifications");
    ble_client->disconnect();
    data_in = nullptr;
    return false;
  }

  String address = address_buffer;
  address.toLowerCase();
  snprintf(mqtt_topic_prefix, sizeof(mqtt_topic_prefix), "%s/%s/",
           MQTT_BASE_TOPIC, address.c_str());
  Serial.printf("BLE: subscribed; MQTT prefix is %s\n", mqtt_topic_prefix);
  Serial.printf("BLE: connected to %s, RSSI %d dBm, MTU %u\n", address_buffer,
                ble_client->getRssi(), ble_client->getMTU());
  return true;
}

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

void ensure_mqtt() {
  if (MQTT_HOST[0] == '\0' || WiFi.status() != WL_CONNECTED ||
      mqtt_client.connected() || mqtt_topic_prefix[0] == '\0') {
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
           mqtt_topic_prefix);
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
             mqtt_topic_prefix);
    mqtt_client.publish(availability_topic, "online", true);
    Serial.println("MQTT: connected");
  } else {
    Serial.printf("MQTT: connection failed, state=%d\n", mqtt_client.state());
  }
}

void publish_pending_measurement() {
  Measurement current{};
  portENTER_CRITICAL(&measurement_mux);
  const bool pending = measurement_pending;
  if (pending) {
    current = pending_measurement;
    measurement_pending = false;
  }
  portEXIT_CRITICAL(&measurement_mux);
  if (!pending) {
    return;
  }

  // The server uses the readings as event triggers for the audio stream.
  if (server_connected) {
    char message[32];
    snprintf(message, sizeof(message), "{\"db\":%.1f}",
             static_cast<double>(current.decibels));
    ws_client.sendTXT(message);
  }

  if (mqtt_client.connected() && mqtt_topic_prefix[0] != '\0') {
    char topic[128];
    char value[24];
    snprintf(topic, sizeof(topic), "%sdba", mqtt_topic_prefix);
    snprintf(value, sizeof(value), "%.1f", current.decibels);
    mqtt_client.publish(topic, value);

    if (!have_published_reading_state ||
        current.battery_low != last_published_reading_state.battery_low ||
        current.hold != last_published_reading_state.hold ||
        strcmp(current.mode, last_published_reading_state.mode) != 0 ||
        strcmp(current.speed, last_published_reading_state.speed) != 0) {
      snprintf(topic, sizeof(topic), "%sbattery_low", mqtt_topic_prefix);
      mqtt_client.publish(topic, current.battery_low ? "1" : "0", true);
      snprintf(topic, sizeof(topic), "%shold", mqtt_topic_prefix);
      mqtt_client.publish(topic, current.hold ? "1" : "0", true);
      snprintf(topic, sizeof(topic), "%smode", mqtt_topic_prefix);
      mqtt_client.publish(topic, current.mode, true);
      snprintf(topic, sizeof(topic), "%sspeed", mqtt_topic_prefix);
      mqtt_client.publish(topic, current.speed, true);
      last_published_reading_state = current;
      have_published_reading_state = true;
    }
    if (!have_published_flags ||
        strcmp(current.flags, last_published_flags) != 0) {
      snprintf(topic, sizeof(topic), "%sflags", mqtt_topic_prefix);
      mqtt_client.publish(topic, current.flags, true);
      snprintf(last_published_flags, sizeof(last_published_flags), "%s",
               current.flags);
      have_published_flags = true;
    }

    const uint32_t now = millis();
    if (now - last_diagnostic_publish_ms >= kDiagnosticPublishIntervalMs) {
      if (ble_client != nullptr && ble_client->isConnected()) {
        snprintf(topic, sizeof(topic), "%srssi", mqtt_topic_prefix);
        snprintf(value, sizeof(value), "%d", ble_client->getRssi());
        mqtt_client.publish(topic, value);
      }
      if (ble_client != nullptr && ble_client->isConnected() &&
          now - last_measurement_ms < 5000 && clock_is_set()) {
        snprintf(topic, sizeof(topic), "%slast_seen", mqtt_topic_prefix);
        snprintf(value, sizeof(value), "%lld",
                 static_cast<long long>(time(nullptr)));
        mqtt_client.publish(topic, value, true);
      }
      last_diagnostic_publish_ms = now;
    }
  }

  Serial.printf("UT353BT: %.1f dBA flags=%s\n", current.decibels,
                current.flags);
}

void publish_ble_connection_state() {
  if (!mqtt_client.connected() || mqtt_topic_prefix[0] == '\0') {
    mqtt_state_pending = true;
    return;
  }

  const bool connected = ble_client != nullptr && ble_client->isConnected();
  if (!mqtt_state_pending && have_ble_connection_state &&
      connected == last_ble_connected) {
    return;
  }

  char topic[128];
  snprintf(topic, sizeof(topic), "%sconnected", mqtt_topic_prefix);
  mqtt_client.publish(topic, connected ? "1" : "0", true);
  last_ble_connected = connected;
  have_ble_connection_state = true;
  mqtt_state_pending = false;
}

// BLE scanning and connecting block for seconds, so they run in their own
// task and never stall the WebSocket/audio loop.
void ble_task(void *) {
  for (;;) {
    if (ble_client == nullptr || !ble_client->isConnected()) {
      data_in = nullptr;
      const uint32_t now = millis();
      if (now - last_scan_ms >= kScanIntervalMs) {
        last_scan_ms = now;
        connect_to_meter();
      }
    } else if (data_in != nullptr &&
               millis() - last_poll_ms >= kPollIntervalMs) {
      static const uint8_t request = '^';
      last_poll_ms = millis();
      if (!data_in->writeValue(&request, sizeof(request), false)) {
        if (++ble_poll_failures % 20 == 1) {
          Serial.printf("BLE: write to ff01 failed (%lu failures)\n",
                        static_cast<unsigned long>(ble_poll_failures));
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

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

  const uint32_t notifications = ble_notifications;
  const uint32_t bytes = audio_bytes_sent;
  const bool wifi_up = WiFi.status() == WL_CONNECTED;
  const bool meter_up = ble_client != nullptr && ble_client->isConnected();

  Serial.printf("Status: wifi=%s ip=%s server=%s meter=%s audio=%s\n",
                wifi_up ? "up" : "down",
                wifi_up ? WiFi.localIP().toString().c_str() : "-",
                server_connected ? "up" : "down", meter_up ? "up" : "down",
                audio_ready ? "ok" : "off");
  if (meter_up) {
    Serial.printf("  BLE: %lu readings/s, last %.1f dBA, parse errors %lu, "
                  "RSSI %d dBm\n",
                  static_cast<unsigned long>((notifications -
                                              last_notifications) /
                                             seconds),
                  static_cast<double>(last_db),
                  static_cast<unsigned long>(ble_parse_errors),
                  ble_client->getRssi());
  }
  {
    // A floating/missing mic reads as constant 0 or a stuck value, so the
    // peak never varies between chunks; a live mic always has noise.
    const uint16_t peak = audio_peak;
    const uint16_t low = audio_min_peak;
    const char *verdict = !mic_present ? "NOT STREAMING (no mic signal)"
                          : peak < 8 ? "NO SIGNAL (mic missing/wired wrong?)"
                          : (peak == low ? "constant value (mic not driving data?)"
                                         : "signal present");
    Serial.printf("  Mic: peak %u, min chunk peak %u -> %s\n", peak, low, verdict);
    audio_peak = 0;
    audio_min_peak = 0xFFFF;
  }
  if (server_connected) {
    Serial.printf("  Audio: %lu kbit/s sent, buffer %u/%u bytes\n",
                  static_cast<unsigned long>((bytes - last_bytes) * 8 /
                                             seconds / 1000),
                  static_cast<unsigned>(kAudioStreamBufferBytes -
                                        xStreamBufferSpacesAvailable(
                                            audio_stream)),
                  static_cast<unsigned>(kAudioStreamBufferBytes));
  }
  Serial.printf("  Heap: %lu free, min %lu\n",
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getMinFreeHeap()));
  last_notifications = notifications;
  last_bytes = bytes;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);  // lets the serial monitor attach before the banner
  Serial.printf("Reset reason: %d\n", static_cast<int>(esp_reset_reason()));
  Serial.printf("UT353BT node starting; server %s:%d, meter %s\n", SERVER_HOST,
                static_cast<int>(SERVER_PORT),
                METER_MAC[0] != '\0' ? METER_MAC : "(any)");

  if (WIFI_SSID[0] == '\0' || SERVER_HOST[0] == '\0' ||
      SERVER_TOKEN[0] == '\0') {
    Serial.println("Configure WIFI_SSID, SERVER_HOST and SERVER_TOKEN in "
                   "include/secrets.h");
    return;
  }

  if (MQTT_HOST[0] != '\0') {
    if (MQTT_TLS) {
      secure_network_client.setCACert(kServerCaCert);
      mqtt_client.setClient(secure_network_client);
    } else {
      mqtt_client.setClient(network_client);
    }
    mqtt_client.setServer(MQTT_HOST, MQTT_PORT);
    mqtt_client.setBufferSize(256);
  }
  ensure_wifi();
  if (setup_audio() &&
      xTaskCreate(audio_task, "audio_stream", 6144, nullptr, 2, nullptr) !=
          pdPASS) {
    Serial.println("Audio: failed to start stream task");
    audio_ready = false;
  }

  NimBLEDevice::init("");
  NimBLEDevice::setPower(9);
  xTaskCreate(ble_task, "ble", 8192, nullptr, 1, nullptr);
}

void loop() {
  if (WIFI_SSID[0] == '\0' || SERVER_HOST[0] == '\0' ||
      SERVER_TOKEN[0] == '\0') {
    delay(1000);
    return;
  }

  ensure_wifi();
  ensure_mqtt();
  if (MQTT_HOST[0] != '\0') {
    mqtt_client.loop();
    publish_ble_connection_state();
  }
  if (audio_ready) {
    start_server_stream();
    ws_client.loop();
    forward_audio_to_server();
  }

  publish_pending_measurement();
  print_status();
  delay(5);
}
