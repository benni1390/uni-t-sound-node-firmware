#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "measurement.h"

namespace {

// Frame layout: AA BB <2 bytes> ';' <9 chars display> '=' <4 status bytes>.
std::vector<uint8_t> make_frame(const char *display, uint32_t status = 0) {
  std::vector<uint8_t> frame(19, 0);
  frame[0] = 0xAA;
  frame[1] = 0xBB;
  frame[4] = ';';
  std::string text(display);
  text.resize(9, ' ');
  memcpy(&frame[5], text.data(), 9);
  frame[14] = '=';
  frame[15] = status >> 24;
  frame[16] = status >> 16;
  frame[17] = status >> 8;
  frame[18] = status;
  return frame;
}

bool parse(const std::vector<uint8_t> &frame, Measurement &out) {
  return parse_measurement(frame.data(), frame.size(), out);
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_parses_valid_reading() {
  Measurement m{};
  TEST_ASSERT_TRUE(parse(make_frame(" 63.2dBA"), m));
  TEST_ASSERT_EQUAL_FLOAT(63.2f, m.decibels);
  TEST_ASSERT_FALSE(m.hold);
  TEST_ASSERT_FALSE(m.battery_low);
  TEST_ASSERT_EQUAL_STRING("normal", m.mode);
  TEST_ASSERT_EQUAL_STRING("unknown", m.speed);
  TEST_ASSERT_EQUAL_STRING("", m.flags);
}

void test_rejects_short_frame() {
  Measurement m{};
  std::vector<uint8_t> frame = make_frame(" 63.2dBA");
  TEST_ASSERT_FALSE(parse_measurement(frame.data(), 18, m));
  TEST_ASSERT_FALSE(parse_measurement(frame.data(), 0, m));
}

void test_rejects_bad_header_and_separators() {
  Measurement m{};
  auto bad_header = make_frame(" 63.2dBA");
  bad_header[0] = 0x00;
  TEST_ASSERT_FALSE(parse(bad_header, m));

  auto bad_second = make_frame(" 63.2dBA");
  bad_second[1] = 0x00;
  TEST_ASSERT_FALSE(parse(bad_second, m));

  auto bad_semicolon = make_frame(" 63.2dBA");
  bad_semicolon[4] = ':';
  TEST_ASSERT_FALSE(parse(bad_semicolon, m));

  auto bad_equals = make_frame(" 63.2dBA");
  bad_equals[14] = '-';
  TEST_ASSERT_FALSE(parse(bad_equals, m));
}

void test_rejects_missing_unit_or_number() {
  Measurement m{};
  TEST_ASSERT_FALSE(parse(make_frame(" 63.2"), m));
  TEST_ASSERT_FALSE(parse(make_frame("    dBA"), m));
  TEST_ASSERT_FALSE(parse(make_frame(" abc dBA"), m));
  TEST_ASSERT_FALSE(parse(make_frame(" 6x.2dBA"), m));
}

void test_rejects_out_of_range_values() {
  Measurement m{};
  TEST_ASSERT_FALSE(parse(make_frame("-5.0dBA"), m));
  TEST_ASSERT_FALSE(parse(make_frame("200.1dBA"), m));
  TEST_ASSERT_TRUE(parse(make_frame("200.0dBA"), m));
  TEST_ASSERT_TRUE(parse(make_frame("  0.0dBA"), m));
}

void test_hold_flag() {
  Measurement m{};
  TEST_ASSERT_TRUE(parse(make_frame(" 63.2dBA", 1u << 16), m));
  TEST_ASSERT_TRUE(m.hold);
  TEST_ASSERT_EQUAL_STRING("hold", m.flags);
}

void test_battery_low_from_either_bit() {
  Measurement m{};
  TEST_ASSERT_TRUE(parse(make_frame(" 63.2dBA", 1u << 22), m));
  TEST_ASSERT_TRUE(m.battery_low);
  TEST_ASSERT_TRUE(parse(make_frame(" 63.2dBA", 1u << 8), m));
  TEST_ASSERT_TRUE(m.battery_low);
}

void test_min_and_max_modes() {
  Measurement m{};
  TEST_ASSERT_TRUE(parse(make_frame(" 63.2dBA", 1u << 18), m));
  TEST_ASSERT_EQUAL_STRING("min", m.mode);
  TEST_ASSERT_EQUAL_STRING("min", m.flags);

  TEST_ASSERT_TRUE(parse(make_frame(" 63.2dBA", 2u << 18), m));
  TEST_ASSERT_EQUAL_STRING("max", m.mode);
  TEST_ASSERT_EQUAL_STRING("max", m.flags);

  TEST_ASSERT_TRUE(parse(make_frame(" 63.2dBA", 3u << 18), m));
  TEST_ASSERT_EQUAL_STRING("unknown", m.mode);
}

void test_fast_and_slow_speed() {
  Measurement m{};
  TEST_ASSERT_TRUE(parse(make_frame(" 63.2dBA", 4u << 24), m));
  TEST_ASSERT_EQUAL_STRING("fast", m.speed);
  TEST_ASSERT_EQUAL_STRING("fast", m.flags);

  TEST_ASSERT_TRUE(parse(make_frame(" 63.2dBA", 3u << 24), m));
  TEST_ASSERT_EQUAL_STRING("slow", m.speed);
  TEST_ASSERT_EQUAL_STRING("", m.flags);
}

void test_flags_are_comma_separated() {
  Measurement m{};
  const uint32_t status = (1u << 16) | (2u << 18) | (4u << 24) | (1u << 17);
  TEST_ASSERT_TRUE(parse(make_frame(" 63.2dBA", status), m));
  TEST_ASSERT_EQUAL_STRING("hold,unk,max,fast", m.flags);
}

void test_flags_stay_within_buffer() {
  Measurement m{};
  TEST_ASSERT_TRUE(parse(make_frame(" 63.2dBA", 0xFFFFFFFFu), m));
  TEST_ASSERT_TRUE(strlen(m.flags) < sizeof(m.flags));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parses_valid_reading);
  RUN_TEST(test_rejects_short_frame);
  RUN_TEST(test_rejects_bad_header_and_separators);
  RUN_TEST(test_rejects_missing_unit_or_number);
  RUN_TEST(test_rejects_out_of_range_values);
  RUN_TEST(test_hold_flag);
  RUN_TEST(test_battery_low_from_either_bit);
  RUN_TEST(test_min_and_max_modes);
  RUN_TEST(test_fast_and_slow_speed);
  RUN_TEST(test_flags_are_comma_separated);
  RUN_TEST(test_flags_stay_within_buffer);
  return UNITY_END();
}
