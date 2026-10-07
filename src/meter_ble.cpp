#include "meter_ble.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>

#include <cstdio>
#include <strings.h>

#include "config.h"

namespace {

constexpr char kDeviceName[] = "UT353BT";
constexpr char kServiceUuid[] = "0000ff12-0000-1000-8000-00805f9b34fb";
constexpr char kDataInUuid[] = "0000ff01-0000-1000-8000-00805f9b34fb";
constexpr char kDataOutUuid[] = "0000ff02-0000-1000-8000-00805f9b34fb";
constexpr uint32_t kScanIntervalMs = 5000;
constexpr uint32_t kPollIntervalMs = 125;

NimBLEClient *ble_client = nullptr;
NimBLERemoteCharacteristic *data_in = nullptr;
portMUX_TYPE measurement_mux = portMUX_INITIALIZER_UNLOCKED;
Measurement pending_measurement{};
bool measurement_pending = false;
char topic_prefix[96] = {};
uint32_t last_scan_ms = 0;
uint32_t last_poll_ms = 0;
uint32_t ble_poll_failures = 0;
volatile uint32_t ble_notifications = 0;
volatile uint32_t ble_parse_errors = 0;
volatile uint32_t last_measurement_ms = 0;
volatile float last_db = -1.0f;

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
  snprintf(topic_prefix, sizeof(topic_prefix), "%s/%s/",
           MQTT_BASE_TOPIC, address.c_str());
  Serial.printf("BLE: subscribed; MQTT prefix is %s\n", topic_prefix);
  Serial.printf("BLE: connected to %s, RSSI %d dBm, MTU %u\n", address_buffer,
                ble_client->getRssi(), ble_client->getMTU());
  return true;
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

}  // namespace

void meter_start() {
  NimBLEDevice::init("");
  NimBLEDevice::setPower(9);
  xTaskCreate(ble_task, "ble", 8192, nullptr, 1, nullptr);
}

bool meter_connected() {
  return ble_client != nullptr && ble_client->isConnected();
}

int meter_rssi() { return meter_connected() ? ble_client->getRssi() : 0; }

bool meter_take_measurement(Measurement &out) {
  portENTER_CRITICAL(&measurement_mux);
  const bool pending = measurement_pending;
  if (pending) {
    out = pending_measurement;
    measurement_pending = false;
  }
  portEXIT_CRITICAL(&measurement_mux);
  return pending;
}

const char *meter_topic_prefix() { return topic_prefix; }
uint32_t meter_notifications() { return ble_notifications; }
uint32_t meter_parse_errors() { return ble_parse_errors; }
uint32_t meter_last_measurement_ms() { return last_measurement_ms; }
float meter_last_db() { return last_db; }
