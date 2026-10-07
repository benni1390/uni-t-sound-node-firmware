#pragma once

#include <cstddef>
#include <cstdint>

struct Measurement {
  float decibels;
  bool hold;
  bool battery_low;
  char mode[8];
  char speed[8];
  char flags[32];
};

// Parses one UT353BT notification frame. Returns false for malformed data.
bool parse_measurement(const uint8_t *data, size_t length, Measurement &out);
