#pragma once
#define ESP_ARDUINO_VERSION_MAJOR 3
inline bool ledcDetach(uint8_t) { return true; }
inline bool ledcAttach(uint8_t, uint32_t, uint8_t) { return true; }
inline bool ledcWrite(uint8_t, uint32_t) { return true; }
