#include "measurement.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

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
  out.flags[0] = '\0';
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
