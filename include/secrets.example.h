#pragma once

#define WIFI_SSID "your-wifi-ssid"
#define WIFI_PASSWORD "your-wifi-password"

// Receiver service (see the uni-t-sound-node-server repository). Audio and readings
// are streamed here; nothing is stored on the node, so data is lost while the
// server or Wi-Fi is unreachable.
#define SERVER_HOST "192.168.1.10"
#define SERVER_PORT 8080
// true = wss://. The server certificate is validated against the CA in
// ca_cert.h (Let's Encrypt); SERVER_HOST must be the DNS name on the certificate.
#define SERVER_TLS false
// Must match UPLOAD_TOKEN on the server.
#define SERVER_TOKEN "change-me"
// Lowercase letters, digits, '_' and '-'. Empty = "esp32-<wifi mac>".
#define NODE_ID "room1"

// Optional: also publish readings to MQTT. Leave MQTT_HOST empty to disable.
// For the bundled server use its DNS name, port 8883, MQTT_TLS true (validated
// against ca_cert.h, like SERVER_TLS), user "node" and MQTT_PASSWORD from the
// server's .env. For a plain local broker use port 1883 and MQTT_TLS false.
#define MQTT_HOST ""
#define MQTT_PORT 8883
#define MQTT_TLS true
#define MQTT_USER "node"
#define MQTT_PASSWORD ""
#define MQTT_BASE_TOPIC "ut353bt"

// Set a distinct UT353BT MAC address on each room node if meters overlap.
// Leave empty to connect to the first nearby meter advertising as UT353BT.
#define METER_MAC ""

// The server starts/extends a clip when a UT353BT reading reaches this value.
#define SOUND_THRESHOLD_DB 60.0f

// ICS-43434 L/R must be connected to GND (left channel). ESP32-C6 pins; avoid
// strapping pins (4, 5, 8, 9, 15) and USB (12, 13).
#define MIC_I2S_BCLK 2
#define MIC_I2S_WS 3
#define MIC_I2S_DATA 10
