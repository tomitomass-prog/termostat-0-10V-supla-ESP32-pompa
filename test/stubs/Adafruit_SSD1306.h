#pragma once
#include <cstdint>
#include "Wire.h"
class Adafruit_SSD1306 {
 public:
  Adafruit_SSD1306(int, int, TwoWire *, int) {}
  bool begin(int, uint8_t, bool = true, bool = true) { return true; }
  void clearDisplay() {}
  void setTextColor(int) {}
  void setTextSize(int) {}
  void setCursor(int, int) {}
  void drawLine(int, int, int, int, int) {}
  void display() {}
  void print(const char *) {}
  void print(char) {}
  void print(int) {}
  void print(unsigned) {}
  void print(float, int = 2) {}
  void print(double, int = 2) {}
  void println(const char *) {}
  template <typename... Args> int printf(const char *, Args...) { return 0; }
};
