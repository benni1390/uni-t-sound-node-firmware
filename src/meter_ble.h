#pragma once

#include <cstdint>

#include "measurement.h"

// Starts the BLE stack and the task that connects to the meter and polls it.
void meter_start();
bool meter_connected();
int meter_rssi();
// Copies the newest unread reading; returns false if there is none.
bool meter_take_measurement(Measurement &out);
// MQTT topic prefix derived from the meter address; empty until connected.
const char *meter_topic_prefix();
uint32_t meter_notifications();
uint32_t meter_parse_errors();
uint32_t meter_last_measurement_ms();
float meter_last_db();
