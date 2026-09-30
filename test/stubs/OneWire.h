#pragma once
#include <cstddef>
#include <cstdint>
class OneWire {
 public:
  explicit OneWire(uint8_t) {}
  static uint8_t crc8(const uint8_t *, uint8_t) { return 0; }
};
