#pragma once

#include <cstdint>

// Certificate validation needs a valid clock; this also starts NTP.
bool clock_is_set();
// Connects the WebSocket once WiFi (and, with TLS, the clock) is ready.
void server_start();
// Runs the WebSocket and forwards buffered audio to the server.
void server_loop();
bool server_connected();
// Readings let the server trigger clip recording.
void server_send_reading(float decibels);
uint32_t server_audio_bytes_sent();
