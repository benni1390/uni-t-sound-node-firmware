#include "audio.h"

#include <Arduino.h>
#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>

#include "config.h"

namespace {

constexpr size_t kAudioSamplesPerRead = 256;
// A connected mic always shows some noise; a missing one reads as a constant.
constexpr int32_t kMicMinSpan = 4;
constexpr uint32_t kMicHoldMs = 3000;

StreamBufferHandle_t audio_stream = nullptr;
i2s_chan_handle_t rx_channel = nullptr;
volatile bool ready = false;
volatile bool streaming = false;
volatile bool mic_present = false;
volatile uint32_t mic_live_until_ms = 0;
volatile uint16_t audio_peak = 0;
volatile uint16_t audio_min_peak = 0xFFFF;

// Audio is only buffered while streaming is active and the mic is live;
// otherwise samples are dropped, nothing is stored on the node.
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
  if (!streaming || !mic_present) {
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
  ready = true;
  Serial.printf("Audio: %lu Hz mono, streamed to server\n",
                static_cast<unsigned long>(kAudioSampleRate));
  return true;
}

}  // namespace

bool audio_start() {
  if (setup_audio() &&
      xTaskCreate(audio_task, "audio_stream", 6144, nullptr, 2, nullptr) !=
          pdPASS) {
    Serial.println("Audio: failed to start stream task");
    ready = false;
  }
  return ready;
}

bool audio_ready() { return ready; }
bool audio_mic_present() { return mic_present; }
void audio_set_streaming(bool active) { streaming = active; }

size_t audio_read(uint8_t *out, size_t max_bytes) {
  return xStreamBufferReceive(audio_stream, out, max_bytes, 0);
}

void audio_discard() { xStreamBufferReset(audio_stream); }

size_t audio_buffered_bytes() {
  return kAudioStreamBufferBytes - xStreamBufferSpacesAvailable(audio_stream);
}

void audio_take_peaks(uint16_t &peak, uint16_t &min_chunk_peak) {
  peak = audio_peak;
  min_chunk_peak = audio_min_peak;
  audio_peak = 0;
  audio_min_peak = 0xFFFF;
}
