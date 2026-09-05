#pragma once

namespace hvac {
class MideaHvac;
struct State;
}

namespace matter_thermostat {

bool begin(hvac::MideaHvac &hvac);
bool start();
void publishHvacState(const hvac::State &state);
void printCommissioningInfo();

}  // namespace matter_thermostat
