#pragma once
#include <cstddef>
#include <cstdint>
class TwoWire {
 public:
  void begin(uint8_t = 0, uint8_t = 0) {}
  void setClock(uint32_t) {}
  void beginTransmission(uint8_t) {}
  size_t write(uint8_t) { return 1; }
  uint8_t endTransmission() { return 0; }
};
inline TwoWire Wire;
