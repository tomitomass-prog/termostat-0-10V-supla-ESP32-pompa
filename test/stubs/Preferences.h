#pragma once
#include <cstdint>
class Preferences {
 public:
  bool begin(const char *, bool) { return true; }
  int16_t getShort(const char *, int16_t value) { return value; }
  bool getBool(const char *, bool value) { return value; }
  size_t putShort(const char *, int16_t) { return 0; }
  size_t putBool(const char *, bool) { return 0; }
};
