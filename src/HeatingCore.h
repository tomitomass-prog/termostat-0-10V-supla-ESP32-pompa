#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace heating {

enum AlarmFlag : uint32_t {
  ALARM_NONE = 0,
  ALARM_OUTLET_SENSOR = 1u << 0,
  ALARM_INLET_SENSOR = 1u << 1,
  ALARM_OUTSIDE_SENSOR = 1u << 2,
  ALARM_OUTPUT_DRIVER = 1u << 3,
  ALARM_OVERHEAT = 1u << 4,
  ALARM_CONFIG = 1u << 5,
};

inline AlarmFlag operator|(AlarmFlag lhs, AlarmFlag rhs) {
  return static_cast<AlarmFlag>(static_cast<uint32_t>(lhs) |
                                static_cast<uint32_t>(rhs));
}

inline uint32_t addAlarm(uint32_t alarms, AlarmFlag flag) {
  return alarms | static_cast<uint32_t>(flag);
}

struct Settings {
  // Krzywa: T_wyj = T_SUPLA + slope * max(0, balance - T_zew) + correction.
  float curveSlope = 0.60f;
  float balanceTemperatureC = 20.0f;
  float curveCorrectionC = 0.0f;
  float targetMinC = 20.0f;
  float targetMaxC = 70.0f;

  // Wspolczynniki w jednostkach procent wyjscia.
  float kp = 10.0f;             // % / degC
  float ki = 0.02f;             // % / (degC*s)
  float kd = 0.0f;              // %*s / degC
  float derivativeFilter = 0.20f;  // 0..1, 1 = brak filtrowania
  float slewRatePctPerMin = 100.0f;

  float outletSafetyMaxC = 75.0f;
  float inletSafetyMaxC = 90.0f;
  float safetyHysteresisC = 3.0f;
};

struct Inputs {
  bool enabled = false;
  bool weatherEnabled = true;
  bool outputDriverReady = true;

  float suplaSetpointC = 30.0f;
  float outletC = NAN;
  float inletC = NAN;
  float outsideC = NAN;

  bool outletValid = false;
  bool inletValid = false;
  bool outsideValid = false;
  float dtSeconds = 1.0f;
};

struct Result {
  float targetC = NAN;
  float outputPct = 0.0f;
  float pTerm = 0.0f;
  float iTerm = 0.0f;
  float dTerm = 0.0f;
  uint32_t alarms = ALARM_NONE;
  bool weatherFallback = false;
  bool safetyStopped = false;
};

class Controller {
 public:
  explicit Controller(const Settings &settings = Settings{})
      : settings_(sanitize(settings)) {}

  void setSettings(const Settings &settings) {
    settings_ = sanitize(settings);
    integral_ = clamp(integral_, 0.0f, 100.0f);
  }

  const Settings &settings() const { return settings_; }

  void reset(float currentOutputPct = 0.0f) {
    initialized_ = false;
    integral_ = 0.0f;
    filteredDerivative_ = 0.0f;
    lastMeasurement_ = NAN;
    lastOutput_ = clamp(currentOutputPct, 0.0f, 100.0f);
    overheatLatched_ = false;
  }

  Result update(const Inputs &input) {
    Result result;
    const float dt = clamp(input.dtSeconds, 0.05f, 30.0f);

    if (!input.outputDriverReady) {
      result.alarms = addAlarm(result.alarms, ALARM_OUTPUT_DRIVER);
    }
    if (!input.outletValid || !std::isfinite(input.outletC)) {
      result.alarms = addAlarm(result.alarms, ALARM_OUTLET_SENSOR);
    }
    if (!input.inletValid || !std::isfinite(input.inletC)) {
      result.alarms = addAlarm(result.alarms, ALARM_INLET_SENSOR);
    }
    if (!input.outsideValid || !std::isfinite(input.outsideC)) {
      result.alarms = addAlarm(result.alarms, ALARM_OUTSIDE_SENSOR);
    }

    result.targetC = calculateTarget(input, result.weatherFallback);

    // Zatrzask temperaturowy ma pierwszenstwo przed PID.
    const bool outletTooHot =
        input.outletValid && std::isfinite(input.outletC) &&
        input.outletC >= settings_.outletSafetyMaxC;
    const bool inletTooHot =
        input.inletValid && std::isfinite(input.inletC) &&
        input.inletC >= settings_.inletSafetyMaxC;
    if (outletTooHot || inletTooHot) {
      overheatLatched_ = true;
    }

    if (overheatLatched_) {
      // Zatrzask mozna skasowac dopiero po wiarygodnym potwierdzeniu,
      // ze oba punkty pomiarowe sa ponizej progow. Zanik czujnika nie jest
      // traktowany jako ostygniecie instalacji.
      const bool outletSafe =
          input.outletValid && std::isfinite(input.outletC) &&
          input.outletC <=
              settings_.outletSafetyMaxC - settings_.safetyHysteresisC;
      const bool inletSafe =
          input.inletValid && std::isfinite(input.inletC) &&
          input.inletC <=
              settings_.inletSafetyMaxC - settings_.safetyHysteresisC;
      if (outletSafe && inletSafe) {
        overheatLatched_ = false;
      }
    }

    if (overheatLatched_) {
      result.alarms = addAlarm(result.alarms, ALARM_OVERHEAT);
    }

    const bool fatal = !input.enabled || !input.outputDriverReady ||
                       !input.outletValid ||
                       !std::isfinite(input.outletC) || overheatLatched_ ||
                       !std::isfinite(result.targetC);
    if (fatal) {
      result.outputPct = 0.0f;
      result.safetyStopped = input.enabled;
      initialized_ = false;
      integral_ = 0.0f;
      filteredDerivative_ = 0.0f;
      lastMeasurement_ = input.outletC;
      lastOutput_ = 0.0f;
      return result;
    }

    const float error = result.targetC - input.outletC;
    result.pTerm = settings_.kp * error;

    if (!initialized_) {
      // Start bez skoku: calka dobierana do ostatniego wyjscia.
      integral_ = clamp(lastOutput_ - result.pTerm, 0.0f, 100.0f);
      lastMeasurement_ = input.outletC;
      filteredDerivative_ = 0.0f;
      initialized_ = true;
    }

    const float measurementRate = (input.outletC - lastMeasurement_) / dt;
    const float derivativeRaw = -settings_.kd * measurementRate;
    const float alpha = clamp(settings_.derivativeFilter, 0.0f, 1.0f);
    filteredDerivative_ += alpha * (derivativeRaw - filteredDerivative_);
    result.dTerm = filteredDerivative_;

    // Calka warunkowa (anti-windup). Najpierw sprawdzamy przewidywane nasycenie.
    const float candidateIntegral =
        clamp(integral_ + settings_.ki * error * dt, 0.0f, 100.0f);
    const float candidateRaw =
        result.pTerm + candidateIntegral + result.dTerm;
    const bool wouldSaturateHigh = candidateRaw > 100.0f && error > 0.0f;
    const bool wouldSaturateLow = candidateRaw < 0.0f && error < 0.0f;
    if (!wouldSaturateHigh && !wouldSaturateLow) {
      integral_ = candidateIntegral;
    }
    result.iTerm = integral_;

    const float requested =
        clamp(result.pTerm + result.iTerm + result.dTerm, 0.0f, 100.0f);
    float output = requested;
    if (settings_.slewRatePctPerMin > 0.0f) {
      const float maxStep = settings_.slewRatePctPerMin * dt / 60.0f;
      output = clamp(requested, lastOutput_ - maxStep, lastOutput_ + maxStep);
    }

    result.outputPct = clamp(output, 0.0f, 100.0f);
    lastMeasurement_ = input.outletC;
    lastOutput_ = result.outputPct;
    return result;
  }

 private:
  static float clamp(float value, float minimum, float maximum) {
    return std::max(minimum, std::min(maximum, value));
  }

  static Settings sanitize(Settings settings) {
    settings.curveSlope = clamp(settings.curveSlope, 0.0f, 5.0f);
    settings.balanceTemperatureC =
        clamp(settings.balanceTemperatureC, -10.0f, 30.0f);
    settings.curveCorrectionC = clamp(settings.curveCorrectionC, -20.0f, 20.0f);
    settings.targetMinC = clamp(settings.targetMinC, 5.0f, 89.0f);
    settings.targetMaxC = clamp(settings.targetMaxC, 5.0f, 90.0f);
    if (settings.targetMaxC < settings.targetMinC + 1.0f) {
      settings.targetMaxC = settings.targetMinC + 1.0f;
    }
    settings.kp = clamp(settings.kp, 0.0f, 100.0f);
    settings.ki = clamp(settings.ki, 0.0f, 10.0f);
    settings.kd = clamp(settings.kd, 0.0f, 1000.0f);
    settings.derivativeFilter = clamp(settings.derivativeFilter, 0.0f, 1.0f);
    settings.slewRatePctPerMin =
        clamp(settings.slewRatePctPerMin, 0.0f, 6000.0f);
    settings.outletSafetyMaxC =
        clamp(settings.outletSafetyMaxC, 20.0f, 100.0f);
    settings.inletSafetyMaxC =
        clamp(settings.inletSafetyMaxC, 20.0f, 120.0f);
    settings.safetyHysteresisC =
        clamp(settings.safetyHysteresisC, 0.5f, 20.0f);
    return settings;
  }

  float calculateTarget(const Inputs &input, bool &weatherFallback) const {
    if (!std::isfinite(input.suplaSetpointC)) {
      return NAN;
    }

    float target = input.suplaSetpointC;
    weatherFallback = false;
    if (input.weatherEnabled) {
      if (input.outsideValid && std::isfinite(input.outsideC)) {
        const float weatherDemand =
            std::max(0.0f, settings_.balanceTemperatureC - input.outsideC);
        target += settings_.curveSlope * weatherDemand +
                  settings_.curveCorrectionC;
      } else {
        // Brak pogody nie zatrzymuje ogrzewania: zostaje nastawa z SUPLA.
        weatherFallback = true;
      }
    }
    return clamp(target, settings_.targetMinC, settings_.targetMaxC);
  }

  Settings settings_;
  bool initialized_ = false;
  bool overheatLatched_ = false;
  float integral_ = 0.0f;
  float filteredDerivative_ = 0.0f;
  float lastMeasurement_ = NAN;
  float lastOutput_ = 0.0f;
};

}  // namespace heating
