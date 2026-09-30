#pragma once
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define HEX 16
#define SSD1306_WHITE 1
#define SSD1306_SWITCHCAPVCC 2

using byte = uint8_t;
inline uint32_t millis() { static uint32_t v = 0; return ++v; }
inline void delay(uint32_t) {}
inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int digitalRead(uint8_t) { return HIGH; }

struct SerialStub {
  void begin(unsigned long) {}
  void print(const char *) {}
  void print(char) {}
  void print(int) {}
  void print(unsigned) {}
  void print(float, int = 2) {}
  void print(double, int = 2) {}
  void print(uint8_t, int) {}
  void println() {}
  void println(const char *) {}
  template <typename... Args> int printf(const char *, Args...) { return 0; }
};
inline SerialStub Serial;
