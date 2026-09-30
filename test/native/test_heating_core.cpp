#include <cassert>
#include <cmath>
#include <iostream>

#include "../../src/HeatingCore.h"

namespace {
bool near(float a, float b, float tolerance = 0.01f) {
  return std::fabs(a - b) <= tolerance;
}

heating::Inputs validInputs() {
  heating::Inputs in;
  in.enabled = true;
  in.outputDriverReady = true;
  in.suplaSetpointC = 30.0f;
  in.outletC = 25.0f;
  in.inletC = 60.0f;
  in.outsideC = 10.0f;
  in.outletValid = true;
  in.inletValid = true;
  in.outsideValid = true;
  in.dtSeconds = 1.0f;
  return in;
}
}  // namespace

int main() {
  heating::Settings settings;
  settings.curveSlope = 0.6f;
  settings.balanceTemperatureC = 20.0f;
  settings.curveCorrectionC = 0.0f;
  settings.slewRatePctPerMin = 6000.0f;  // bez ograniczenia w testach
  heating::Controller controller(settings);

  {
    auto in = validInputs();
    in.weatherEnabled = false;
    const auto result = controller.update(in);
    assert(near(result.targetC, 30.0f));
  }

  controller.reset();
  {
    auto in = validInputs();
    in.weatherEnabled = true;
    const auto result = controller.update(in);
    assert(near(result.targetC, 36.0f));
    assert(result.outputPct > 0.0f);
  }

  controller.reset();
  {
    auto in = validInputs();
    in.weatherEnabled = true;
    in.outsideValid = false;
    in.outsideC = NAN;
    const auto result = controller.update(in);
    assert(near(result.targetC, 30.0f));
    assert(result.weatherFallback);
    assert(result.alarms & heating::ALARM_OUTSIDE_SENSOR);
    assert(result.outputPct > 0.0f);
  }

  controller.reset();
  {
    auto in = validInputs();
    in.outletValid = false;
    in.outletC = NAN;
    const auto result = controller.update(in);
    assert(near(result.outputPct, 0.0f));
    assert(result.alarms & heating::ALARM_OUTLET_SENSOR);
    assert(result.safetyStopped);
  }

  controller.reset();
  {
    auto in = validInputs();
    in.outletC = 76.0f;
    const auto result = controller.update(in);
    assert(near(result.outputPct, 0.0f));
    assert(result.alarms & heating::ALARM_OVERHEAT);
  }

  controller.reset();
  {
    auto in = validInputs();
    in.outletC = 76.0f;
    auto result = controller.update(in);
    assert(result.alarms & heating::ALARM_OVERHEAT);

    in.outletValid = false;
    in.outletC = NAN;
    result = controller.update(in);
    assert(result.alarms & heating::ALARM_OVERHEAT);
    assert(near(result.outputPct, 0.0f));
  }

  controller.reset();
  {
    auto in = validInputs();
    in.outletC = 0.0f;
    for (int i = 0; i < 10000; ++i) {
      const auto result = controller.update(in);
      assert(result.outputPct >= 0.0f && result.outputPct <= 100.0f);
      assert(result.iTerm >= 0.0f && result.iTerm <= 100.0f);
    }
  }

  controller.reset();
  {
    auto in = validInputs();
    in.outputDriverReady = false;
    const auto result = controller.update(in);
    assert(near(result.outputPct, 0.0f));
    assert(result.alarms & heating::ALARM_OUTPUT_DRIVER);
  }

  std::cout << "HeatingCore: wszystkie testy zaliczone\n";
  return 0;
}
