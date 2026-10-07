#pragma once

#include <cstddef>
#include <cstdint>

constexpr uint32_t kAudioSampleRate = 16000;
constexpr size_t kAudioStreamBufferBytes = 16384;

// Sets up the I2S microphone and its capture task. Returns false on failure.
bool audio_start();
bool audio_ready();
// True while the microphone shows a live signal (not floating or stuck).
bool audio_mic_present();
// Samples are only buffered while a consumer is connected.
void audio_set_streaming(bool active);
size_t audio_read(uint8_t *out, size_t max_bytes);
void audio_discard();
size_t audio_buffered_bytes();
// Returns the peak range since the last call and resets it.
void audio_take_peaks(uint16_t &peak, uint16_t &min_chunk_peak);
