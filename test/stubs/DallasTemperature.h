#pragma once
#include <cstdint>
#include "OneWire.h"
typedef uint8_t DeviceAddress[8];
#define TEMPERATURE_NOT_AVAILABLE -275.0
class DallasTemperature {
 public:
  explicit DallasTemperature(OneWire *) {}
  void begin() {}
  void setWaitForConversion(bool) {}
  void setResolution(const uint8_t *, uint8_t) {}
  void requestTemperatures() {}
  float getTempC(const uint8_t *) { return 20.0f; }
  uint8_t getDeviceCount() const { return 0; }
  bool getAddress(uint8_t *, uint8_t) { return false; }
};
