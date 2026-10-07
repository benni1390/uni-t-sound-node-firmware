#pragma once

// Build-time settings. include/secrets.h overrides these defaults.

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
