#pragma once
#include <cstdint>
#include <cstring>

#define SUPLA_CHANNELFNC_HVAC_THERMOSTAT 100
#define SUPLA_CHANNELFNC_POWERSWITCH 101
#define SUPLA_HVAC_MODE_OFF 0
#define SUPLA_HVAC_MODE_HEAT 1
#define SUPLA_HVAC_SUBFUNCTION_HEAT 1

class SuplaDeviceClass {
 public:
  void setName(const char *) {}
  void setSwVersion(const char *) {}
  void setCustomHostnamePrefix(const char *) {}
  void begin(int = 0) {}
  void iterate() {}
  void setInitialMode(int) {}
};
inline SuplaDeviceClass SuplaDevice;

namespace Supla {
namespace InitialMode { constexpr int StartInCfgMode = 1; }
class Eeprom { public: void setStateSavePeriod(uint32_t) {} };
class ESPWifi {};
class LittleFsConfig {};
class EspWebServer {};
namespace Device { class StatusLed { public: StatusLed(uint8_t, bool) {} }; }
namespace Control {
class HvacBase {
 public:
  HvacBase() = default;
  void setHeatingAndCoolingSupported(bool) {}
  void setDefaultSubfunction(uint8_t) {}
  void setTemperatureRoomMin(int16_t) {}
  void setTemperatureRoomMax(int16_t) {}
  void setDefaultTemperatureRoomMin(int32_t, int16_t) {}
  void setDefaultTemperatureRoomMax(int32_t, int16_t) {}
  void setTemperatureSetpointChangeSwitchesToManualMode(bool) {}
  void setTemperatureSetpointHeat(int) {}
  void setTargetMode(int, bool = false) {}
  bool setMainThermometerChannelNo(int16_t) { return true; }
  int getTemperatureSetpointHeat() { return 3000; }
  int getMode() { return SUPLA_HVAC_MODE_HEAT; }
  bool isThermostatDisabled() { return false; }
};
class VirtualRelay {
 public:
  void setDefaultFunction(int) {}
  void setDefaultStateOn() {}
  bool isOn() const { return true; }
  void turnOn() {}
  void turnOff() {}
};
class Button {
 public:
  Button(uint8_t, bool, bool) {}
  void configureAsConfigButton(SuplaDeviceClass *) {}
};
}
namespace Sensor {
class VirtualThermometer { public: void setValue(double) {} };
class GeneralPurposeMeasurement {
 public:
  void setDefaultUnitAfterValue(const char *) {}
  void setDefaultValuePrecision(uint8_t) {}
  void setDefaultRefreshIntervalMs(int32_t) {}
  void setValue(const double &) {}
};
}
namespace Html {
class DeviceInfo { public: explicit DeviceInfo(SuplaDeviceClass *) {} };
class WifiParameters {};
class ProtocolParameters {};
class StatusLedParameters {};
class HvacParameters { public: explicit HvacParameters(Control::HvacBase *) {} };
template <typename T> class CustomParameterTemplate {
 public:
  CustomParameterTemplate(const char *, const char *, T = T{}, T = T{}, T = T{}, uint8_t = 0) {}
  T getParameterValue() { return T{}; }
  void setParameterValue(T) {}
};
using CustomParameter = CustomParameterTemplate<int32_t>;
class CustomTextParameter {
 public:
  CustomTextParameter(const char *, const char *, int) {}
  bool getParameterValue(char *, const int) { return false; }
  void setParameterValue(const char *) {}
};
}
}
