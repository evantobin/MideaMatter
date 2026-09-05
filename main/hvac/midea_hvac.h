#pragma once

#include <functional>

namespace hvac {

enum class Mode : unsigned char { Off, Heat, Cool, Auto, Dry, FanOnly };
enum class FanSpeed : unsigned char { Auto, Low, Medium, High };
enum class Swing : unsigned char { Off, Vertical, Horizontal, Both };

struct State {
  Mode mode = Mode::Off;
  FanSpeed fanSpeed = FanSpeed::Auto;
  Swing swing = Swing::Off;
  float indoorTemperatureC = 0.0f;
  float targetTemperatureC = 23.0f;
};

class MideaHvac {
 public:
  using StateCallback = std::function<void(const State &)>;

  void begin();
  void poll();
  void queueCommand(Mode mode, float targetTemperatureC);
  void queueFanSpeed(FanSpeed fanSpeed);
  void queueSwing(Swing swing);
  void setStateCallback(StateCallback callback);
  void publishState();

 private:
  StateCallback stateCallback_;
};

}  // namespace hvac
