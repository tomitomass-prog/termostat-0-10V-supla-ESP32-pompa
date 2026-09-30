/*
 * Sterownik pogodowy zaworu 3-drogowego dla ESP32 + SUPLA.
 *
 * Sprzet referencyjny:
 *   - ESP32 DevKit V1 (4 MB flash)
 *   - 3 x DS18B20 na GPIO4 (rezystor 4.7 kOhm do 3.3 V)
 *   - OLED SSD1306 128x64 I2C, adres 0x3C
 *   - GP8403 I2C 0-10 V, adres 0x58 (preferowany)
 *     albo zewnetrzny konwerter PWM -> 0-10 V na GPIO25
 *   - przekaźnik pompy GPIO26 (polaryzacja konfigurowalna)
 *   - przycisk zmiany ekranu GPIO27 -> GND
 *   - przycisk konfiguracji SUPLA GPIO0 (BOOT)
 *
 * UWAGA: 0-10 V nigdy nie wolno laczyc bezposrednio z GPIO ESP32.
 */

#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Wire.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <SuplaDevice.h>
#include <supla/control/button.h>
#include <supla/control/hvac_base.h>
#include <supla/control/virtual_relay.h>
#include <supla/device/status_led.h>
#include <supla/network/esp_web_server.h>
#include <supla/network/esp_wifi.h>
#include <supla/network/html/custom_parameter.h>
#include <supla/network/html/custom_text_parameter.h>
#include <supla/network/html/device_info.h>
#include <supla/network/html/hvac_parameters.h>
#include <supla/network/html/protocol_parameters.h>
#include <supla/network/html/status_led_parameters.h>
#include <supla/network/html/wifi_parameters.h>
#include <supla/sensor/general_purpose_measurement.h>
#include <supla/sensor/virtual_thermometer.h>
#include <supla/storage/eeprom.h>
#include <supla/storage/littlefs_config.h>

#include "HeatingCore.h"

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#else
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif

namespace {

// -----------------------------------------------------------------------------
// Piny i stale sprzetowe
// -----------------------------------------------------------------------------
constexpr uint8_t PIN_ONEWIRE = 4;
constexpr uint8_t PIN_I2C_SDA = 21;
constexpr uint8_t PIN_I2C_SCL = 22;
constexpr uint8_t PIN_PWM_0_10V = 25;
constexpr uint8_t PIN_PUMP_RELAY = 26;
constexpr uint8_t PIN_DISPLAY_BUTTON = 27;
constexpr uint8_t PIN_CONFIG_BUTTON = 0;
constexpr uint8_t PIN_STATUS_LED = 2;

constexpr uint8_t OLED_ADDRESS = 0x3C;
constexpr uint8_t GP8403_ADDRESS = 0x58;
constexpr uint8_t PWM_CHANNEL = 0;
constexpr uint8_t PWM_RESOLUTION_BITS = 12;
constexpr uint16_t PWM_MAX_DUTY = (1u << PWM_RESOLUTION_BITS) - 1u;

constexpr uint32_t SENSOR_REQUEST_PERIOD_MS = 2000;
constexpr uint32_t SENSOR_CONVERSION_MS = 800;
constexpr uint32_t SENSOR_STALE_MS = 15000;
constexpr uint32_t CONTROL_PERIOD_MS = 1000;
constexpr uint32_t DISPLAY_PERIOD_MS = 400;
constexpr uint32_t SETTINGS_SAVE_DELAY_MS = 5000;
constexpr uint32_t LONG_PRESS_MS = 1800;
constexpr uint8_t DISPLAY_PAGE_COUNT = 6;

// Adresy z dostarczonego YAML, zapisane w kolejnosci bajtow magistrali Dallas.
// Wyjscie mieszacza: 0x8200000f7b576b28
constexpr char DEFAULT_ADDR_OUTLET[] = "286B577B0F000082";
// Czujnik kotla z YAML zostal przyjety jako domyslny czujnik wejscia zaworu.
constexpr char DEFAULT_ADDR_INLET[] = "28C4C17A0F00005E";
// Temperatura zewnetrzna: 0x7700000f7a5f1f28
constexpr char DEFAULT_ADDR_OUTSIDE[] = "281F5F7A0F000077";

constexpr int16_t DEFAULT_SETPOINT_X100 = 3000;  // 30.00 C
constexpr int16_t DEFAULT_PUMP_TMIN_X100 = 3500;  // 35.00 C

// -----------------------------------------------------------------------------
// SUPLA, pamiec i kanaly
// -----------------------------------------------------------------------------
Supla::Eeprom eeprom;
Supla::ESPWifi wifi;
Supla::LittleFsConfig configSupla;
Supla::Device::StatusLed statusLed(PIN_STATUS_LED, true);
Supla::EspWebServer suplaWebServer;
Preferences preferences;

Supla::Control::HvacBase *hvac = nullptr;
Supla::Control::VirtualRelay *weatherSwitch = nullptr;
Supla::Control::HvacBase *pumpMinThermostat = nullptr;
Supla::Control::VirtualRelay *pumpAutoSwitch = nullptr;
Supla::Control::VirtualRelay *pumpManualSwitch = nullptr;
Supla::Sensor::VirtualThermometer *outletChannel = nullptr;
Supla::Sensor::VirtualThermometer *inletChannel = nullptr;
Supla::Sensor::VirtualThermometer *outsideChannel = nullptr;
Supla::Sensor::VirtualThermometer *targetChannel = nullptr;
Supla::Sensor::GeneralPurposeMeasurement *outputPercentChannel = nullptr;
Supla::Sensor::GeneralPurposeMeasurement *outputVoltageChannel = nullptr;
Supla::Sensor::GeneralPurposeMeasurement *alarmChannel = nullptr;
Supla::Sensor::GeneralPurposeMeasurement *driverChannel = nullptr;
Supla::Sensor::GeneralPurposeMeasurement *pumpStateChannel = nullptr;

// -----------------------------------------------------------------------------
// Parametry lokalnego portalu konfiguracyjnego SUPLA
// -----------------------------------------------------------------------------
using FloatParameter = Supla::Html::CustomParameterTemplate<float>;
using IntParameter = Supla::Html::CustomParameter;

FloatParameter *paramCurve = nullptr;
FloatParameter *paramBalance = nullptr;
FloatParameter *paramCorrection = nullptr;
FloatParameter *paramTargetMin = nullptr;
FloatParameter *paramTargetMax = nullptr;
FloatParameter *paramKp = nullptr;
FloatParameter *paramKi = nullptr;
FloatParameter *paramKd = nullptr;
FloatParameter *paramDerivativeFilter = nullptr;
FloatParameter *paramSlew = nullptr;
FloatParameter *paramSafetyOutlet = nullptr;
FloatParameter *paramSafetyInlet = nullptr;
FloatParameter *paramVoltageMin = nullptr;
FloatParameter *paramVoltageMax = nullptr;
IntParameter *paramOutputMode = nullptr;
IntParameter *paramOutputReverse = nullptr;
IntParameter *paramPwmFrequency = nullptr;
FloatParameter *paramPumpHysteresis = nullptr;
IntParameter *paramPumpRelayActiveHigh = nullptr;
Supla::Html::CustomTextParameter *paramAddressOutlet = nullptr;
Supla::Html::CustomTextParameter *paramAddressInlet = nullptr;
Supla::Html::CustomTextParameter *paramAddressOutside = nullptr;

// -----------------------------------------------------------------------------
// Konfiguracja robocza
// -----------------------------------------------------------------------------
enum class RequestedOutputMode : uint8_t {
  Auto = 0,
  GP8403 = 1,
  PWM = 2,
};

enum class ActiveOutputDriver : uint8_t {
  None = 0,
  GP8403 = 1,
  PWM = 2,
};

struct RuntimeSettings {
  heating::Settings control;
  RequestedOutputMode outputMode = RequestedOutputMode::Auto;
  bool reverseOutput = false;
  float minimumVoltage = 0.0f;
  float maximumVoltage = 10.0f;
  uint32_t pwmFrequencyHz = 1000;
  float pumpHysteresisC = 2.0f;
  bool pumpRelayActiveHigh = false;  // domyslnie modul aktywny LOW
};

RuntimeSettings settings;
heating::Controller controller;
heating::Result lastControlResult;

// -----------------------------------------------------------------------------
// Narzedzia
// -----------------------------------------------------------------------------
template <typename T>
T clampValue(T value, T minimum, T maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

bool elapsed(uint32_t now, uint32_t since, uint32_t period) {
  return static_cast<uint32_t>(now - since) >= period;
}

bool validTemperature(float value) {
  return std::isfinite(value) && value >= -55.0f && value <= 125.0f &&
         fabsf(value - 85.0f) > 0.01f;
}

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parseDallasAddress(const char *text, DeviceAddress result) {
  if (!text) return false;
  while (*text == ' ' || *text == '\t') ++text;
  if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text += 2;

  char compact[17] = {};
  size_t length = 0;
  for (const char *p = text; *p != '\0'; ++p) {
    if (hexDigit(*p) >= 0) {
      if (length >= 16) return false;
      compact[length++] = *p;
    } else if (*p == ':' || *p == '-' || *p == ' ' || *p == 'x' ||
               *p == 'X') {
      continue;
    } else {
      return false;
    }
  }
  if (length != 16) return false;

  for (size_t i = 0; i < 8; ++i) {
    const int high = hexDigit(compact[i * 2]);
    const int low = hexDigit(compact[i * 2 + 1]);
    if (high < 0 || low < 0) return false;
    result[i] = static_cast<uint8_t>((high << 4) | low);
  }
  return result[0] == 0x28 && OneWire::crc8(result, 7) == result[7];
}

void printAddress(const DeviceAddress address) {
  for (uint8_t i = 0; i < 8; ++i) {
    if (address[i] < 0x10) Serial.print('0');
    Serial.print(address[i], HEX);
  }
}

// -----------------------------------------------------------------------------
// Wyjscie 0-10 V: GP8403 lub PWM
// -----------------------------------------------------------------------------
class AnalogValveOutput {
 public:
  void begin(const RuntimeSettings &cfg) {
    requestedMode_ = cfg.outputMode;
    reverse_ = cfg.reverseOutput;
    minimumVoltage_ = cfg.minimumVoltage;
    maximumVoltage_ = cfg.maximumVoltage;
    pwmFrequencyHz_ = cfg.pwmFrequencyHz;

    pinMode(PIN_PWM_0_10V, OUTPUT);
    digitalWrite(PIN_PWM_0_10V, LOW);
    activeDriver_ = ActiveOutputDriver::None;
    gp8403Detected_ = probeGp8403();

    if (requestedMode_ == RequestedOutputMode::GP8403 ||
        (requestedMode_ == RequestedOutputMode::Auto && gp8403Detected_)) {
      if (gp8403Detected_ && configureGp8403()) {
        activeDriver_ = ActiveOutputDriver::GP8403;
      }
    }

    if (activeDriver_ == ActiveOutputDriver::None &&
        (requestedMode_ == RequestedOutputMode::PWM ||
         requestedMode_ == RequestedOutputMode::Auto)) {
      setupPwm();
      activeDriver_ = ActiveOutputDriver::PWM;
    }

    writePercent(0.0f);
    Serial.printf("Sterownik 0-10 V: %s\n", driverName());
  }

  bool writePercent(float logicalPercent) {
    logicalPercent_ = clampValue(logicalPercent, 0.0f, 100.0f);
    float physicalPercent = reverse_ ? 100.0f - logicalPercent_ : logicalPercent_;
    voltage_ = minimumVoltage_ +
               (maximumVoltage_ - minimumVoltage_) * physicalPercent / 100.0f;
    voltage_ = clampValue(voltage_, 0.0f, 10.0f);

    bool success = false;
    if (activeDriver_ == ActiveOutputDriver::GP8403) {
      // Sterownik jest wybierany tylko podczas startu. Nie przelaczamy na PWM
      // po bledzie I2C, poniewaz oba wyjscia moga byc fizycznie podlaczone do
      // roznych torow, a GP8403 moglby utrzymac poprzednie napiecie.
      success = writeGp8403Voltage(voltage_);
    }
    if (activeDriver_ == ActiveOutputDriver::PWM) {
      success = writePwmVoltage(voltage_);
    }
    lastWriteOk_ = success;
    return success;
  }

  bool ready() const {
    return activeDriver_ != ActiveOutputDriver::None && lastWriteOk_;
  }

  float logicalPercent() const { return logicalPercent_; }
  float voltage() const { return voltage_; }
  ActiveOutputDriver driver() const { return activeDriver_; }

  const char *driverName() const {
    switch (activeDriver_) {
      case ActiveOutputDriver::GP8403:
        return "GP8403";
      case ActiveOutputDriver::PWM:
        return "PWM";
      default:
        return "BRAK";
    }
  }

 private:
  bool probeGp8403() {
    Wire.beginTransmission(GP8403_ADDRESS);
    return Wire.endTransmission() == 0;
  }

  bool writeGpRegister(uint8_t reg, const uint8_t *data, size_t size) {
    Wire.beginTransmission(GP8403_ADDRESS);
    Wire.write(reg);
    for (size_t i = 0; i < size; ++i) Wire.write(data[i]);
    return Wire.endTransmission() == 0;
  }

  bool configureGp8403() {
    // Rejestr 0x01, 0x11 = zakres 0-10 V dla obu kanalow.
    const uint8_t range = 0x11;
    if (!writeGpRegister(0x01, &range, 1)) return false;
    return writeGp8403Voltage(0.0f);
  }

  bool writeGp8403Voltage(float voltage) {
    const uint16_t value12 = static_cast<uint16_t>(lroundf(
        clampValue(voltage, 0.0f, 10.0f) * 4095.0f / 10.0f));
    const uint16_t wireValue = static_cast<uint16_t>(value12 << 4);
    const uint8_t data[2] = {
        static_cast<uint8_t>(wireValue & 0xFF),
        static_cast<uint8_t>((wireValue >> 8) & 0xFF),
    };
    // Rejestr 0x02 = kanal 0.
    return writeGpRegister(0x02, data, sizeof(data));
  }

  void setupPwm() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcDetach(PIN_PWM_0_10V);
    ledcAttach(PIN_PWM_0_10V, pwmFrequencyHz_, PWM_RESOLUTION_BITS);
#else
    ledcSetup(PWM_CHANNEL, pwmFrequencyHz_, PWM_RESOLUTION_BITS);
    ledcAttachPin(PIN_PWM_0_10V, PWM_CHANNEL);
#endif
    writePwmVoltage(0.0f);
  }

  bool writePwmVoltage(float voltage) {
    const uint32_t duty = static_cast<uint32_t>(lroundf(
        clampValue(voltage, 0.0f, 10.0f) * PWM_MAX_DUTY / 10.0f));
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    return ledcWrite(PIN_PWM_0_10V, duty);
#else
    ledcWrite(PWM_CHANNEL, duty);
    return true;
#endif
  }

  RequestedOutputMode requestedMode_ = RequestedOutputMode::Auto;
  ActiveOutputDriver activeDriver_ = ActiveOutputDriver::None;
  bool gp8403Detected_ = false;
  bool reverse_ = false;
  bool lastWriteOk_ = false;
  float minimumVoltage_ = 0.0f;
  float maximumVoltage_ = 10.0f;
  float logicalPercent_ = 0.0f;
  float voltage_ = 0.0f;
  uint32_t pwmFrequencyHz_ = 1000;
};

AnalogValveOutput valveOutput;

// -----------------------------------------------------------------------------
// Czujniki temperatury
// -----------------------------------------------------------------------------
enum TemperatureIndex : uint8_t {
  SENSOR_OUTLET = 0,
  SENSOR_INLET = 1,
  SENSOR_OUTSIDE = 2,
  SENSOR_COUNT = 3,
};

struct TemperatureState {
  DeviceAddress address = {};
  float value = NAN;
  bool hasValue = false;
  uint32_t lastValidMs = 0;
  float filterTimeConstantS = 5.0f;
};

OneWire oneWire(PIN_ONEWIRE);
DallasTemperature dallas(&oneWire);
TemperatureState temperature[SENSOR_COUNT];
bool temperatureRequestPending = false;
uint32_t lastTemperatureRequestMs = 0;
uint32_t temperatureRequestStartedMs = 0;
bool sensorAddressConfigValid = true;

void updateFilteredTemperature(TemperatureState &state,
                               float raw,
                               uint32_t now,
                               float sampleSeconds) {
  if (!validTemperature(raw)) return;
  if (!state.hasValue || !std::isfinite(state.value)) {
    state.value = raw;
  } else {
    const float alpha = sampleSeconds /
                        (state.filterTimeConstantS + sampleSeconds);
    state.value += clampValue(alpha, 0.01f, 1.0f) * (raw - state.value);
  }
  state.hasValue = true;
  state.lastValidMs = now;
}

bool sensorIsValid(TemperatureIndex index, uint32_t now) {
  const TemperatureState &state = temperature[index];
  return state.hasValue && validTemperature(state.value) &&
         !elapsed(now, state.lastValidMs, SENSOR_STALE_MS);
}

void publishTemperatureChannel(Supla::Sensor::VirtualThermometer *channel,
                               TemperatureIndex index,
                               uint32_t now) {
  if (!channel) return;
  if (sensorIsValid(index, now)) {
    channel->setValue(temperature[index].value);
  } else {
    channel->setValue(TEMPERATURE_NOT_AVAILABLE);
  }
}

void requestTemperatures(uint32_t now) {
  dallas.requestTemperatures();
  temperatureRequestStartedMs = now;
  lastTemperatureRequestMs = now;
  temperatureRequestPending = true;
}

void completeTemperatureRead(uint32_t now) {
  const float sampleSeconds = SENSOR_REQUEST_PERIOD_MS / 1000.0f;
  for (uint8_t i = 0; i < SENSOR_COUNT; ++i) {
    const float raw = dallas.getTempC(temperature[i].address);
    updateFilteredTemperature(temperature[i], raw, now, sampleSeconds);
  }
  publishTemperatureChannel(outletChannel, SENSOR_OUTLET, now);
  publishTemperatureChannel(inletChannel, SENSOR_INLET, now);
  publishTemperatureChannel(outsideChannel, SENSOR_OUTSIDE, now);
  temperatureRequestPending = false;
}

void serviceTemperatureBus(uint32_t now) {
  if (temperatureRequestPending &&
      elapsed(now, temperatureRequestStartedMs, SENSOR_CONVERSION_MS)) {
    completeTemperatureRead(now);
  }
  if (!temperatureRequestPending &&
      elapsed(now, lastTemperatureRequestMs, SENSOR_REQUEST_PERIOD_MS)) {
    requestTemperatures(now);
  }
}

void scanDallasBus() {
  Serial.printf("DS18B20 znalezione: %u\n", dallas.getDeviceCount());
  DeviceAddress address;
  for (uint8_t i = 0; i < dallas.getDeviceCount(); ++i) {
    if (dallas.getAddress(address, i)) {
      Serial.print("  ");
      printAddress(address);
      Serial.println();
    }
  }
}

// -----------------------------------------------------------------------------
// Pompa obiegowa - czujnik wejscia zaworu jest temperatura bufora
// -----------------------------------------------------------------------------
bool currentHvacEnabled();  // deklaracja funkcji z sekcji regulatora
bool pumpOutputState = false;
bool pumpLowBufferBlocked = true;

float pumpMinimumTemperatureC() {
  if (!pumpMinThermostat) return DEFAULT_PUMP_TMIN_X100 / 100.0f;
  int value = pumpMinThermostat->getTemperatureSetpointHeat();
  if (value < 500 || value > 9000) value = DEFAULT_PUMP_TMIN_X100;
  return value / 100.0f;
}

void writePumpRelay(bool on) {
  pumpOutputState = on;
  const bool physicalHigh = settings.pumpRelayActiveHigh ? on : !on;
  digitalWrite(PIN_PUMP_RELAY, physicalHigh ? HIGH : LOW);
}

void runPumpControl(uint32_t now) {
  bool desired = false;
  const bool autoMode = pumpAutoSwitch && pumpAutoSwitch->isOn();

  if (!autoMode) {
    // W trybie recznym drugi przelacznik w SUPLA bezposrednio steruje pompa.
    desired = pumpManualSwitch && pumpManualSwitch->isOn();
  } else {
    const bool bufferValid = sensorAddressConfigValid &&
                             sensorIsValid(SENSOR_INLET, now);
    const bool heatingEnabled = currentHvacEnabled();

    if (!bufferValid || !heatingEnabled) {
      // Brak wiarygodnej temperatury lub regulator STOP -> pompa OFF.
      desired = false;
      pumpLowBufferBlocked = true;
    } else {
      const float bufferC = temperature[SENSOR_INLET].value;
      const float tminC = pumpMinimumTemperatureC();

      // Histereza: ponowne zalaczenie dopiero po Tmin + histereza.
      if (pumpLowBufferBlocked) {
        if (bufferC >= tminC + settings.pumpHysteresisC) {
          pumpLowBufferBlocked = false;
        }
      } else if (bufferC < tminC) {
        pumpLowBufferBlocked = true;
      }
      desired = !pumpLowBufferBlocked;
    }
  }

  writePumpRelay(desired);
  if (pumpStateChannel) {
    pumpStateChannel->setValue(pumpOutputState ? 1.0 : 0.0);
  }
}

// -----------------------------------------------------------------------------
// OLED i przycisk
// -----------------------------------------------------------------------------
Adafruit_SSD1306 display(128, 64, &Wire, -1);
bool displayReady = false;
uint8_t displayPage = 0;
bool displayButtonStable = HIGH;
bool displayButtonLastRaw = HIGH;
uint32_t displayButtonChangedMs = 0;
uint32_t displayButtonPressedMs = 0;
bool displayLongPressHandled = false;
uint32_t lastDisplayMs = 0;

void drawHeader(const char *title) {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(title);
  display.setCursor(96, 0);
  display.print(WiFi.status() == WL_CONNECTED ? "WIFI" : "OFF");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
}

void drawDisplay() {
  if (!displayReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  const bool weather = weatherSwitch && weatherSwitch->isOn();
  const bool hvacEnabled = hvac && !hvac->isThermostatDisabled() &&
                           hvac->getMode() != SUPLA_HVAC_MODE_OFF;
  const float setpoint = hvac ? hvac->getTemperatureSetpointHeat() / 100.0f : NAN;

  switch (displayPage) {
    case 0:
      drawHeader("WYJSCIE 0-10V");
      display.setTextSize(3);
      display.setCursor(0, 17);
      display.print(valveOutput.logicalPercent(), 0);
      display.print('%');
      display.setTextSize(1);
      display.setCursor(0, 51);
      display.print(valveOutput.voltage(), 2);
      display.print(" V  ");
      display.print(valveOutput.driverName());
      display.setCursor(92, 51);
      display.print(hvacEnabled ? "PRACA" : "STOP");
      break;

    case 1:
      drawHeader("TEMPERATURY");
      display.setCursor(0, 16);
      display.print("SUPLA: ");
      display.print(setpoint, 1);
      display.print(" C");
      display.setCursor(0, 29);
      display.print("Cel:   ");
      display.print(lastControlResult.targetC, 1);
      display.print(" C");
      display.setCursor(0, 42);
      display.print("Wyj:   ");
      display.print(temperature[SENSOR_OUTLET].value, 1);
      display.print(" C");
      display.setCursor(0, 55);
      display.print(weather ? "Pogoda: ON" : "Pogoda: OFF");
      break;

    case 2:
      drawHeader("ZAWOR 3-DROGOWY");
      display.setCursor(0, 18);
      display.print("Wejscie: ");
      display.print(temperature[SENSOR_INLET].value, 1);
      display.print(" C");
      display.setCursor(0, 34);
      display.print("Wyjscie: ");
      display.print(temperature[SENSOR_OUTLET].value, 1);
      display.print(" C");
      display.setCursor(0, 50);
      display.print("Roznica: ");
      display.print(temperature[SENSOR_INLET].value -
                        temperature[SENSOR_OUTLET].value,
                    1);
      display.print(" C");
      break;

    case 3:
      drawHeader("KRZYWA GRZEWCZA");
      display.setCursor(0, 18);
      display.print("Zewn: ");
      display.print(temperature[SENSOR_OUTSIDE].value, 1);
      display.print(" C");
      display.setCursor(0, 33);
      display.print("Nachyl: ");
      display.print(settings.control.curveSlope, 2);
      display.setCursor(0, 48);
      display.print("Punkt: ");
      display.print(settings.control.balanceTemperatureC, 1);
      display.print(" C");
      break;

    case 4:
      drawHeader("POMPA / BUFOR");
      display.setCursor(0, 16);
      display.print("Bufor: ");
      display.print(temperature[SENSOR_INLET].value, 1);
      display.print(" C");
      display.setCursor(0, 29);
      display.print("Tmin:  ");
      display.print(pumpMinimumTemperatureC(), 1);
      display.print(" C");
      display.setCursor(0, 42);
      display.print("Hys:   ");
      display.print(settings.pumpHysteresisC, 1);
      display.print(" C");
      display.setCursor(0, 55);
      if (pumpAutoSwitch && pumpAutoSwitch->isOn()) {
        display.print("AUTO ");
      } else {
        display.print("RECZ ");
      }
      display.print(pumpOutputState ? "POMPA ON" : "POMPA OFF");
      break;

    default:
      drawHeader("PID / ALARM");
      display.setCursor(0, 15);
      display.printf("P:%6.1f I:%6.1f", lastControlResult.pTerm,
                     lastControlResult.iTerm);
      display.setCursor(0, 29);
      display.printf("D:%6.1f", lastControlResult.dTerm);
      display.setCursor(0, 43);
      display.printf("Alarm: 0x%02lX",
                     static_cast<unsigned long>(lastControlResult.alarms));
      display.setCursor(0, 56);
      display.print(lastControlResult.safetyStopped ? "BLOKADA" : "OK");
      break;
  }
  display.display();
}

void toggleWeatherLocally() {
  if (!weatherSwitch) return;
  if (weatherSwitch->isOn()) {
    weatherSwitch->turnOff();
  } else {
    weatherSwitch->turnOn();
  }
}

void serviceDisplayButton(uint32_t now) {
  const bool raw = digitalRead(PIN_DISPLAY_BUTTON);
  if (raw != displayButtonLastRaw) {
    displayButtonLastRaw = raw;
    displayButtonChangedMs = now;
  }
  if (!elapsed(now, displayButtonChangedMs, 30)) return;

  if (raw != displayButtonStable) {
    displayButtonStable = raw;
    if (displayButtonStable == LOW) {
      displayButtonPressedMs = now;
      displayLongPressHandled = false;
    } else if (!displayLongPressHandled) {
      displayPage = (displayPage + 1) % DISPLAY_PAGE_COUNT;
      drawDisplay();
    }
  }

  if (displayButtonStable == LOW && !displayLongPressHandled &&
      elapsed(now, displayButtonPressedMs, LONG_PRESS_MS)) {
    displayLongPressHandled = true;
    toggleWeatherLocally();
    drawDisplay();
  }
}

// -----------------------------------------------------------------------------
// Ladowanie ustawien portalu
// -----------------------------------------------------------------------------
void ensureTextParameter(Supla::Html::CustomTextParameter *parameter,
                         const char *defaultValue,
                         char *destination,
                         size_t destinationSize) {
  if (!parameter || !destination || destinationSize == 0) return;
  if (!parameter->getParameterValue(destination,
                                    static_cast<int>(destinationSize))) {
    parameter->setParameterValue(defaultValue);
    strncpy(destination, defaultValue, destinationSize - 1);
    destination[destinationSize - 1] = '\0';
  }
}

void loadRuntimeSettings() {
  settings.control.curveSlope = paramCurve->getParameterValue();
  settings.control.balanceTemperatureC = paramBalance->getParameterValue();
  settings.control.curveCorrectionC = paramCorrection->getParameterValue();
  settings.control.targetMinC = paramTargetMin->getParameterValue();
  settings.control.targetMaxC = paramTargetMax->getParameterValue();
  settings.control.kp = paramKp->getParameterValue();
  settings.control.ki = paramKi->getParameterValue();
  settings.control.kd = paramKd->getParameterValue();
  settings.control.derivativeFilter =
      paramDerivativeFilter->getParameterValue();
  settings.control.slewRatePctPerMin = paramSlew->getParameterValue();
  settings.control.outletSafetyMaxC = paramSafetyOutlet->getParameterValue();
  settings.control.inletSafetyMaxC = paramSafetyInlet->getParameterValue();

  settings.outputMode = static_cast<RequestedOutputMode>(
      clampValue<int32_t>(paramOutputMode->getParameterValue(), 0, 2));
  settings.reverseOutput = paramOutputReverse->getParameterValue() != 0;
  settings.minimumVoltage = paramVoltageMin->getParameterValue();
  settings.maximumVoltage = paramVoltageMax->getParameterValue();
  if (settings.maximumVoltage < settings.minimumVoltage + 0.1f) {
    settings.maximumVoltage = settings.minimumVoltage + 0.1f;
  }
  settings.pwmFrequencyHz = static_cast<uint32_t>(
      clampValue<int32_t>(paramPwmFrequency->getParameterValue(), 100, 20000));
  settings.pumpHysteresisC = clampValue<float>(
      paramPumpHysteresis->getParameterValue(), 0.0f, 20.0f);
  settings.pumpRelayActiveHigh =
      paramPumpRelayActiveHigh->getParameterValue() != 0;
  controller.setSettings(settings.control);

  char outletText[32] = {};
  char inletText[32] = {};
  char outsideText[32] = {};
  ensureTextParameter(paramAddressOutlet, DEFAULT_ADDR_OUTLET, outletText,
                      sizeof(outletText));
  ensureTextParameter(paramAddressInlet, DEFAULT_ADDR_INLET, inletText,
                      sizeof(inletText));
  ensureTextParameter(paramAddressOutside, DEFAULT_ADDR_OUTSIDE, outsideText,
                      sizeof(outsideText));

  sensorAddressConfigValid =
      parseDallasAddress(outletText, temperature[SENSOR_OUTLET].address) &&
      parseDallasAddress(inletText, temperature[SENSOR_INLET].address) &&
      parseDallasAddress(outsideText, temperature[SENSOR_OUTSIDE].address);

  Serial.println("Ustawienia regulatora:");
  Serial.printf("  krzywa %.2f, punkt %.1f C, korekta %.1f C\n",
                settings.control.curveSlope,
                settings.control.balanceTemperatureC,
                settings.control.curveCorrectionC);
  Serial.printf("  PID Kp %.3f, Ki %.4f, Kd %.3f\n",
                settings.control.kp,
                settings.control.ki,
                settings.control.kd);
  Serial.printf("  wyjscie tryb %u, %.1f-%.1f V, odwrocone %s\n",
                static_cast<unsigned>(settings.outputMode),
                settings.minimumVoltage,
                settings.maximumVoltage,
                settings.reverseOutput ? "tak" : "nie");
  Serial.printf("  adresy czujnikow: %s\n",
                sensorAddressConfigValid ? "poprawne" : "BLAD");
  Serial.printf("  pompa: Tmin %.1f C, histereza %.1f C, aktywny %s\n",
                pumpMinimumTemperatureC(), settings.pumpHysteresisC,
                settings.pumpRelayActiveHigh ? "HIGH" : "LOW");
}

void createConfigurationPage() {
  new Supla::Html::DeviceInfo(&SuplaDevice);
  new Supla::Html::WifiParameters;
  new Supla::Html::ProtocolParameters;
  new Supla::Html::StatusLedParameters;

  paramCurve = new FloatParameter("curve", "Nachylenie krzywej [0-3]",
                                  0.60f, 0.0f, 3.0f, 2);
  paramBalance = new FloatParameter("balance", "Punkt pogodowy C [-10..30]",
                                    20.0f, -10.0f, 30.0f, 1);
  paramCorrection = new FloatParameter("corr", "Korekta krzywej C [-20..20]",
                                       0.0f, -20.0f, 20.0f, 1);
  paramTargetMin = new FloatParameter("tmin", "Minimalny cel C [5..80]",
                                      20.0f, 5.0f, 80.0f, 1);
  paramTargetMax = new FloatParameter("tmax", "Maksymalny cel C [10..90]",
                                      70.0f, 10.0f, 90.0f, 1);

  paramKp = new FloatParameter("kp", "PID Kp [%/C]", 10.0f, 0.0f, 100.0f, 3);
  paramKi = new FloatParameter("ki", "PID Ki [%/(C*s)]", 0.02f, 0.0f, 10.0f, 4);
  paramKd = new FloatParameter("kd", "PID Kd [%*s/C]", 0.0f, 0.0f, 1000.0f, 3);
  paramDerivativeFilter = new FloatParameter(
      "dfilt", "Filtr D [0..1]", 0.20f, 0.0f, 1.0f, 2);
  paramSlew = new FloatParameter("slew", "Szybkosc wyjscia [%/min]",
                                 100.0f, 0.0f, 6000.0f, 1);

  paramSafetyOutlet = new FloatParameter(
      "safeout", "Alarm temp. wyjscia C", 75.0f, 20.0f, 100.0f, 1);
  paramSafetyInlet = new FloatParameter(
      "safein", "Alarm temp. wejscia C", 90.0f, 20.0f, 120.0f, 1);

  paramOutputMode = new IntParameter(
      "outmode", "Wyjscie 0=Auto 1=GP8403 2=PWM", 0, 0, 2);
  paramOutputReverse = new IntParameter(
      "outrev", "Odwroc wyjscie 0=nie 1=tak", 0, 0, 1);
  paramVoltageMin = new FloatParameter(
      "vmin", "Napiecie dla 0% [V]", 0.0f, 0.0f, 9.9f, 2);
  paramVoltageMax = new FloatParameter(
      "vmax", "Napiecie dla 100% [V]", 10.0f, 0.1f, 10.0f, 2);
  paramPwmFrequency = new IntParameter(
      "pwmhz", "Czestotliwosc PWM [Hz]", 1000, 100, 20000);

  paramPumpHysteresis = new FloatParameter(
      "phys", "Pompa - histereza Tmin bufora [C]", 2.0f, 0.0f, 20.0f, 1);
  paramPumpRelayActiveHigh = new IntParameter(
      "ppol", "Pompa - przekaznik 1=aktywny HIGH 0=aktywny LOW", 0, 0, 1);

  paramAddressOutlet = new Supla::Html::CustomTextParameter(
      "addr_out", "DS18B20 wyjscie (16 HEX)", 24);
  paramAddressInlet = new Supla::Html::CustomTextParameter(
      "addr_in", "DS18B20 wejscie (16 HEX)", 24);
  paramAddressOutside = new Supla::Html::CustomTextParameter(
      "addr_ext", "DS18B20 zewnetrzny (16 HEX)", 24);
}

void configureGpmChannel(Supla::Sensor::GeneralPurposeMeasurement *channel,
                         const char *unit,
                         uint8_t precision) {
  if (!channel) return;
  channel->setDefaultUnitAfterValue(unit);
  channel->setDefaultValuePrecision(precision);
  channel->setDefaultRefreshIntervalMs(1000);
}

void configurePumpMinThermostat(int16_t initialSetpoint) {
  pumpMinThermostat = new Supla::Control::HvacBase();
  pumpMinThermostat->setHeatingAndCoolingSupported(true);
  pumpMinThermostat->setDefaultSubfunction(SUPLA_HVAC_SUBFUNCTION_HEAT);
  pumpMinThermostat->setTemperatureRoomMin(500);
  pumpMinThermostat->setTemperatureRoomMax(9000);
  pumpMinThermostat->setDefaultTemperatureRoomMin(
      SUPLA_CHANNELFNC_HVAC_THERMOSTAT, 500);
  pumpMinThermostat->setDefaultTemperatureRoomMax(
      SUPLA_CHANNELFNC_HVAC_THERMOSTAT, 9000);
  pumpMinThermostat->setTemperatureSetpointChangeSwitchesToManualMode(true);
  pumpMinThermostat->setTemperatureSetpointHeat(initialSetpoint);
  pumpMinThermostat->setTargetMode(SUPLA_HVAC_MODE_HEAT);
  new Supla::Html::HvacParameters(pumpMinThermostat);
}

void createSuplaChannels(int16_t savedSetpoint, bool savedEnabled) {
  // Kanal 0: termostat. Jego nastawa jest najwazniejsza dla algorytmu lokalnego.
  hvac = new Supla::Control::HvacBase();
  // HvacBase bez fizycznego OutputInterface nie dobiera funkcji automatycznie.
  // Deklarujemy obsluge termostatu i domyslna podfunkcje grzania, aby kanal
  // pojawil sie w aplikacji SUPLA jako ogrzewanie z nastawa temperatury.
  hvac->setHeatingAndCoolingSupported(true);
  hvac->setDefaultSubfunction(SUPLA_HVAC_SUBFUNCTION_HEAT);
  hvac->setTemperatureRoomMin(1000);
  hvac->setTemperatureRoomMax(7000);
  hvac->setDefaultTemperatureRoomMin(SUPLA_CHANNELFNC_HVAC_THERMOSTAT, 1000);
  hvac->setDefaultTemperatureRoomMax(SUPLA_CHANNELFNC_HVAC_THERMOSTAT, 7000);
  hvac->setTemperatureSetpointChangeSwitchesToManualMode(true);
  hvac->setTemperatureSetpointHeat(savedSetpoint);
  hvac->setTargetMode(savedEnabled ? SUPLA_HVAC_MODE_HEAT : SUPLA_HVAC_MODE_OFF);
  new Supla::Html::HvacParameters(hvac);

  // Kanaly 1-3: fizyczne czujniki.
  outletChannel = new Supla::Sensor::VirtualThermometer;
  inletChannel = new Supla::Sensor::VirtualThermometer;
  outsideChannel = new Supla::Sensor::VirtualThermometer;

  // Kanal 4: wlacznik regulacji pogodowej.
  weatherSwitch = new Supla::Control::VirtualRelay;
  weatherSwitch->setDefaultFunction(SUPLA_CHANNELFNC_POWERSWITCH);
  weatherSwitch->setDefaultStateOn();

  // Kanaly 5-6: procent i napiecie.
  outputPercentChannel = new Supla::Sensor::GeneralPurposeMeasurement;
  configureGpmChannel(outputPercentChannel, "%", 1);
  outputVoltageChannel = new Supla::Sensor::GeneralPurposeMeasurement;
  configureGpmChannel(outputVoltageChannel, "V", 2);

  // Kanal 7: cel po krzywej pogodowej.
  targetChannel = new Supla::Sensor::VirtualThermometer;

  // Kanal 8: maska alarmu, kanal 9: aktywny sterownik (1 GP8403, 2 PWM).
  alarmChannel = new Supla::Sensor::GeneralPurposeMeasurement;
  configureGpmChannel(alarmChannel, "", 0);
  driverChannel = new Supla::Sensor::GeneralPurposeMeasurement;
  configureGpmChannel(driverChannel, "", 0);

  // Kanal 10: zdalna nastawa minimalnej temperatury bufora dla pompy.
  const int16_t savedPumpTmin = clampValue<int16_t>(
      preferences.getShort("pumptmin", DEFAULT_PUMP_TMIN_X100), 500, 9000);
  configurePumpMinThermostat(savedPumpTmin);

  // Kanal 11: AUTO pompy. Kanal 12: reczne ON/OFF gdy AUTO=OFF.
  pumpAutoSwitch = new Supla::Control::VirtualRelay;
  pumpAutoSwitch->setDefaultFunction(SUPLA_CHANNELFNC_POWERSWITCH);
  pumpAutoSwitch->setDefaultStateOn();
  pumpManualSwitch = new Supla::Control::VirtualRelay;
  pumpManualSwitch->setDefaultFunction(SUPLA_CHANNELFNC_POWERSWITCH);

  // Kanal 13: faktyczny stan fizycznego wyjscia pompy (0/1).
  pumpStateChannel = new Supla::Sensor::GeneralPurposeMeasurement;
  configureGpmChannel(pumpStateChannel, "", 0);

  // HvacBase ma wskazywac temperature wyjscia jako glowny pomiar.
  hvac->setMainThermometerChannelNo(1);
  // Nastawa Tmin pompy pokazuje temperature bufora = czujnik przed zaworem.
  pumpMinThermostat->setMainThermometerChannelNo(2);
}

// -----------------------------------------------------------------------------
// Regulator, zapis stanu i publikacja
// -----------------------------------------------------------------------------
uint32_t lastControlMs = 0;
uint32_t runtimeStateChangedMs = 0;
bool runtimeStateDirty = false;
int16_t lastObservedSetpoint = DEFAULT_SETPOINT_X100;
int lastObservedMode = SUPLA_HVAC_MODE_HEAT;
bool lastObservedWeather = true;
int16_t lastObservedPumpTmin = DEFAULT_PUMP_TMIN_X100;
bool lastObservedPumpAuto = true;
bool lastObservedPumpManual = false;

int16_t currentSetpointX100() {
  if (!hvac) return DEFAULT_SETPOINT_X100;
  const int value = hvac->getTemperatureSetpointHeat();
  if (value < 1000 || value > 7000) return lastObservedSetpoint;
  return static_cast<int16_t>(value);
}

bool currentHvacEnabled() {
  if (!hvac) return false;
  return !hvac->isThermostatDisabled() &&
         hvac->getMode() != SUPLA_HVAC_MODE_OFF;
}

void detectRuntimeStateChange(uint32_t now) {
  const int16_t setpoint = currentSetpointX100();
  const int mode = hvac ? hvac->getMode() : SUPLA_HVAC_MODE_OFF;
  const bool weather = weatherSwitch && weatherSwitch->isOn();
  const int16_t pumpTmin = static_cast<int16_t>(
      lroundf(pumpMinimumTemperatureC() * 100.0f));
  const bool pumpAuto = pumpAutoSwitch && pumpAutoSwitch->isOn();
  const bool pumpManual = pumpManualSwitch && pumpManualSwitch->isOn();
  if (setpoint != lastObservedSetpoint || mode != lastObservedMode ||
      weather != lastObservedWeather || pumpTmin != lastObservedPumpTmin ||
      pumpAuto != lastObservedPumpAuto || pumpManual != lastObservedPumpManual) {
    lastObservedSetpoint = setpoint;
    lastObservedMode = mode;
    lastObservedWeather = weather;
    lastObservedPumpTmin = pumpTmin;
    lastObservedPumpAuto = pumpAuto;
    lastObservedPumpManual = pumpManual;
    runtimeStateChangedMs = now;
    runtimeStateDirty = true;
  }
}

void saveRuntimeStateWhenDue(uint32_t now) {
  if (!runtimeStateDirty ||
      !elapsed(now, runtimeStateChangedMs, SETTINGS_SAVE_DELAY_MS)) {
    return;
  }
  preferences.putShort("setpt", lastObservedSetpoint);
  preferences.putBool("enabled", lastObservedMode != SUPLA_HVAC_MODE_OFF);
  preferences.putBool("weather", lastObservedWeather);
  preferences.putShort("pumptmin", lastObservedPumpTmin);
  preferences.putBool("pumpauto", lastObservedPumpAuto);
  preferences.putBool("pumpman", lastObservedPumpManual);
  runtimeStateDirty = false;
}

void publishControlState(uint32_t now) {
  uint32_t alarms = lastControlResult.alarms;
  if (!sensorAddressConfigValid) {
    alarms = heating::addAlarm(alarms, heating::ALARM_CONFIG);
  }
  lastControlResult.alarms = alarms;

  if (targetChannel) {
    targetChannel->setValue(std::isfinite(lastControlResult.targetC)
                                ? lastControlResult.targetC
                                : TEMPERATURE_NOT_AVAILABLE);
  }
  if (outputPercentChannel) {
    outputPercentChannel->setValue(valveOutput.logicalPercent());
  }
  if (outputVoltageChannel) {
    outputVoltageChannel->setValue(valveOutput.voltage());
  }
  if (alarmChannel) alarmChannel->setValue(static_cast<double>(alarms));
  if (driverChannel) {
    driverChannel->setValue(static_cast<double>(valveOutput.driver()));
  }

  publishTemperatureChannel(outletChannel, SENSOR_OUTLET, now);
  publishTemperatureChannel(inletChannel, SENSOR_INLET, now);
  publishTemperatureChannel(outsideChannel, SENSOR_OUTSIDE, now);
}

void runControl(uint32_t now) {
  const float dt = lastControlMs == 0
                       ? 1.0f
                       : clampValue((now - lastControlMs) / 1000.0f, 0.1f, 5.0f);
  lastControlMs = now;

  heating::Inputs input;
  input.enabled = currentHvacEnabled() && sensorAddressConfigValid;
  input.weatherEnabled = weatherSwitch && weatherSwitch->isOn();
  input.outputDriverReady = valveOutput.ready();
  input.suplaSetpointC = currentSetpointX100() / 100.0f;
  input.outletC = temperature[SENSOR_OUTLET].value;
  input.inletC = temperature[SENSOR_INLET].value;
  input.outsideC = temperature[SENSOR_OUTSIDE].value;
  input.outletValid = sensorIsValid(SENSOR_OUTLET, now);
  input.inletValid = sensorIsValid(SENSOR_INLET, now);
  input.outsideValid = sensorIsValid(SENSOR_OUTSIDE, now);
  input.dtSeconds = dt;

  lastControlResult = controller.update(input);
  if (!sensorAddressConfigValid) {
    lastControlResult.alarms = heating::addAlarm(
        lastControlResult.alarms, heating::ALARM_CONFIG);
  }

  // Pierwszy zapis ustala faktyczny stan sterownika. Jezeli komunikacja z DAC
  // zaniknie, kolejna iteracja zatrzyma regulator i zglosi alarm.
  const bool writeOk = valveOutput.writePercent(lastControlResult.outputPct);
  if (!writeOk) {
    lastControlResult.alarms = heating::addAlarm(
        lastControlResult.alarms, heating::ALARM_OUTPUT_DRIVER);
  }
  publishControlState(now);
  runPumpControl(now);
  detectRuntimeStateChange(now);
  saveRuntimeStateWhenDue(now);
}

// -----------------------------------------------------------------------------
// Setup i loop
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("Sterownik pogodowy SUPLA 0-10 V - start");

  pinMode(PIN_DISPLAY_BUTTON, INPUT_PULLUP);
  pinMode(PIN_PWM_0_10V, OUTPUT);
  digitalWrite(PIN_PWM_0_10V, LOW);
  // Domyslna polaryzacja pompy to aktywny LOW, wiec HIGH = bezpieczne OFF.
  pinMode(PIN_PUMP_RELAY, OUTPUT);
  digitalWrite(PIN_PUMP_RELAY, HIGH);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(100000);

  // Magistrala Wire zostala juz uruchomiona na GPIO21/22; periphBegin=false
  // zapobiega ponownej inicjalizacji I2C przez biblioteke OLED.
  displayReady = display.begin(
      SSD1306_SWITCHCAPVCC, OLED_ADDRESS, true, false);
  if (displayReady) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("SUPLA 0-10 V");
    display.println("Start regulatora...");
    display.display();
  } else {
    Serial.println("OLED 0x3C nie odpowiada - regulator pracuje bez ekranu");
  }

  preferences.begin("heatctrl", false);
  int16_t savedSetpoint = preferences.getShort("setpt", DEFAULT_SETPOINT_X100);
  savedSetpoint = clampValue<int16_t>(savedSetpoint, 1000, 7000);
  const bool savedEnabled = preferences.getBool("enabled", true);
  const bool savedWeather = preferences.getBool("weather", true);
  const bool savedPumpAuto = preferences.getBool("pumpauto", true);
  const bool savedPumpManual = preferences.getBool("pumpman", false);

  createConfigurationPage();
  createSuplaChannels(savedSetpoint, savedEnabled);

  auto configButton =
      new Supla::Control::Button(PIN_CONFIG_BUTTON, true, true);
  configButton->configureAsConfigButton(&SuplaDevice);

  eeprom.setStateSavePeriod(5000);
  SuplaDevice.setName("Regulator pogodowy 0-10V");
  SuplaDevice.setSwVersion("1.1.0-pump");
  SuplaDevice.setCustomHostnamePrefix("SUPLA-REG-010V");
  SuplaDevice.setInitialMode(Supla::InitialMode::StartInCfgMode);
  // SuplaDevice v26.4 wymaga co najmniej protokolu 23.
  SuplaDevice.begin(23);

  // Stan lokalny jest zapasowym zrodlem na wypadek braku serwera przy starcie.
  if (weatherSwitch) {
    if (savedWeather) {
      weatherSwitch->turnOn();
    } else {
      weatherSwitch->turnOff();
    }
  }
  if (pumpAutoSwitch) {
    savedPumpAuto ? pumpAutoSwitch->turnOn() : pumpAutoSwitch->turnOff();
  }
  if (pumpManualSwitch) {
    savedPumpManual ? pumpManualSwitch->turnOn() : pumpManualSwitch->turnOff();
  }

  loadRuntimeSettings();
  // Po odczycie polaryzacji ustaw fizyczne wyjscie pompy w stan OFF.
  writePumpRelay(false);
  valveOutput.begin(settings);

  dallas.begin();
  dallas.setWaitForConversion(false);
  for (uint8_t i = 0; i < SENSOR_COUNT; ++i) {
    dallas.setResolution(temperature[i].address, 12);
  }
  temperature[SENSOR_OUTLET].filterTimeConstantS = 5.0f;
  temperature[SENSOR_INLET].filterTimeConstantS = 10.0f;
  temperature[SENSOR_OUTSIDE].filterTimeConstantS = 60.0f;
  scanDallasBus();

  lastObservedSetpoint = currentSetpointX100();
  lastObservedMode = hvac ? hvac->getMode() : SUPLA_HVAC_MODE_OFF;
  lastObservedWeather = weatherSwitch && weatherSwitch->isOn();
  lastObservedPumpTmin = static_cast<int16_t>(
      lroundf(pumpMinimumTemperatureC() * 100.0f));
  lastObservedPumpAuto = pumpAutoSwitch && pumpAutoSwitch->isOn();
  lastObservedPumpManual = pumpManualSwitch && pumpManualSwitch->isOn();

  const uint32_t now = millis();
  lastTemperatureRequestMs = now - SENSOR_REQUEST_PERIOD_MS;
  lastControlMs = now;
  lastDisplayMs = now - DISPLAY_PERIOD_MS;
  controller.reset(0.0f);
  requestTemperatures(now);
  drawDisplay();
}

void loop() {
  SuplaDevice.iterate();
  const uint32_t now = millis();
  serviceTemperatureBus(now);
  serviceDisplayButton(now);

  if (elapsed(now, lastControlMs, CONTROL_PERIOD_MS)) {
    runControl(now);
  }
  if (elapsed(now, lastDisplayMs, DISPLAY_PERIOD_MS)) {
    lastDisplayMs = now;
    drawDisplay();
  }
}

}  // namespace
