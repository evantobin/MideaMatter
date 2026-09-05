#include "matter/matter_thermostat.h"

#include <algorithm>
#include <cmath>

#include <app-common/zap-generated/ids/Attributes.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_attribute_utils.h>
#include <esp_matter_endpoint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <platform/CHIPDeviceLayer.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include "hvac/midea_hvac.h"

namespace matter_thermostat {
namespace {

constexpr float kMinTargetC = 17.0f;
constexpr float kMaxTargetC = 30.0f;
constexpr float kAutoDeadbandC = 2.5f;
constexpr const char *kManufacturer = "DIY";
constexpr const char *kProductName = "Senville Heat Pump";
constexpr const char *kModel = "SENA-18HF UART";

using namespace chip::app::Clusters;

hvac::MideaHvac *sHvac = nullptr;
uint16_t sEndpointId = 0;
portMUX_TYPE sStateMutex = portMUX_INITIALIZER_UNLOCKED;

struct ThermostatState {
  hvac::Mode mode = hvac::Mode::Off;
  hvac::FanSpeed fanSpeed = hvac::FanSpeed::Auto;
  hvac::Swing swing = hvac::Swing::Off;
  float heatingSetpointC = 21.0f;
  float coolingSetpointC = 24.0f;
  float indoorTemperatureC = 0.0f;
  bool pendingReport = false;
};

ThermostatState sState;

uint8_t toMatterMode(hvac::Mode mode) {
  switch (mode) {
    case hvac::Mode::Heat: return static_cast<uint8_t>(Thermostat::SystemModeEnum::kHeat);
    case hvac::Mode::Cool: return static_cast<uint8_t>(Thermostat::SystemModeEnum::kCool);
    case hvac::Mode::Auto: return static_cast<uint8_t>(Thermostat::SystemModeEnum::kAuto);
    case hvac::Mode::Dry: return static_cast<uint8_t>(Thermostat::SystemModeEnum::kDry);
    case hvac::Mode::FanOnly: return static_cast<uint8_t>(Thermostat::SystemModeEnum::kFanOnly);
    default: return static_cast<uint8_t>(Thermostat::SystemModeEnum::kOff);
  }
}

hvac::Mode toHvacMode(uint8_t mode) {
  if (mode == static_cast<uint8_t>(Thermostat::SystemModeEnum::kHeat)) return hvac::Mode::Heat;
  if (mode == static_cast<uint8_t>(Thermostat::SystemModeEnum::kCool)) return hvac::Mode::Cool;
  if (mode == static_cast<uint8_t>(Thermostat::SystemModeEnum::kAuto)) return hvac::Mode::Auto;
  if (mode == static_cast<uint8_t>(Thermostat::SystemModeEnum::kDry)) return hvac::Mode::Dry;
  if (mode == static_cast<uint8_t>(Thermostat::SystemModeEnum::kFanOnly)) return hvac::Mode::FanOnly;
  return hvac::Mode::Off;
}

uint8_t toMatterFanMode(hvac::FanSpeed fanSpeed) {
  switch (fanSpeed) {
    case hvac::FanSpeed::Low: return static_cast<uint8_t>(FanControl::FanModeEnum::kLow);
    case hvac::FanSpeed::Medium: return static_cast<uint8_t>(FanControl::FanModeEnum::kMedium);
    case hvac::FanSpeed::High: return static_cast<uint8_t>(FanControl::FanModeEnum::kHigh);
    default: return static_cast<uint8_t>(FanControl::FanModeEnum::kAuto);
  }
}

hvac::FanSpeed toHvacFanSpeed(uint8_t fanMode) {
  if (fanMode == static_cast<uint8_t>(FanControl::FanModeEnum::kLow)) return hvac::FanSpeed::Low;
  if (fanMode == static_cast<uint8_t>(FanControl::FanModeEnum::kMedium)) return hvac::FanSpeed::Medium;
  if (fanMode == static_cast<uint8_t>(FanControl::FanModeEnum::kHigh) ||
      fanMode == static_cast<uint8_t>(FanControl::FanModeEnum::kOn)) return hvac::FanSpeed::High;
  return hvac::FanSpeed::Auto;
}

uint8_t toMatterRockSetting(hvac::Swing swing) {
  switch (swing) {
    case hvac::Swing::Vertical: return static_cast<uint8_t>(FanControl::RockBitmap::kRockUpDown);
    case hvac::Swing::Horizontal: return static_cast<uint8_t>(FanControl::RockBitmap::kRockLeftRight);
    case hvac::Swing::Both:
      return static_cast<uint8_t>(FanControl::RockBitmap::kRockUpDown) |
             static_cast<uint8_t>(FanControl::RockBitmap::kRockLeftRight);
    default: return 0;
  }
}

hvac::Swing toHvacSwing(uint8_t rockSetting) {
  const uint8_t upDown = static_cast<uint8_t>(FanControl::RockBitmap::kRockUpDown);
  const uint8_t leftRight = static_cast<uint8_t>(FanControl::RockBitmap::kRockLeftRight);
  if ((rockSetting & (upDown | leftRight)) == (upDown | leftRight)) return hvac::Swing::Both;
  if (rockSetting & upDown) return hvac::Swing::Vertical;
  if (rockSetting & leftRight) return hvac::Swing::Horizontal;
  return hvac::Swing::Off;
}

int16_t centiDegrees(float temperature) {
  return static_cast<int16_t>(std::round(temperature * 100.0f));
}

void reportStateOnMatterThread(intptr_t) {
  ThermostatState state;
  portENTER_CRITICAL(&sStateMutex);
  state = sState;
  sState.pendingReport = false;
  portEXIT_CRITICAL(&sStateMutex);

  if (sEndpointId == 0) return;

  esp_matter_attr_val_t value = esp_matter_int16(centiDegrees(state.indoorTemperatureC));
  esp_matter::attribute::update(
      sEndpointId, Thermostat::Id, Thermostat::Attributes::LocalTemperature::Id, &value);

  value = esp_matter_uint8(toMatterMode(state.mode));
  esp_matter::attribute::update(
      sEndpointId, Thermostat::Id, Thermostat::Attributes::SystemMode::Id, &value);

  value = esp_matter_bool(state.mode != hvac::Mode::Off);
  esp_matter::attribute::update(
      sEndpointId, OnOff::Id, OnOff::Attributes::OnOff::Id, &value);

  value = esp_matter_uint8(toMatterFanMode(state.fanSpeed));
  esp_matter::attribute::update(
      sEndpointId, FanControl::Id, FanControl::Attributes::FanMode::Id, &value);

  value = esp_matter_uint8(toMatterRockSetting(state.swing));
  esp_matter::attribute::update(
      sEndpointId, FanControl::Id, FanControl::Attributes::RockSetting::Id, &value);

  value = esp_matter_int16(centiDegrees(state.heatingSetpointC));
  esp_matter::attribute::update(
      sEndpointId, Thermostat::Id, Thermostat::Attributes::OccupiedHeatingSetpoint::Id, &value);

  value = esp_matter_int16(centiDegrees(state.coolingSetpointC));
  esp_matter::attribute::update(
      sEndpointId, Thermostat::Id, Thermostat::Attributes::OccupiedCoolingSetpoint::Id, &value);
}

void scheduleStateReport() {
  bool shouldSchedule = false;
  portENTER_CRITICAL(&sStateMutex);
  if (!sState.pendingReport) {
    sState.pendingReport = true;
    shouldSchedule = true;
  }
  portEXIT_CRITICAL(&sStateMutex);

  if (shouldSchedule) {
    const CHIP_ERROR error = chip::DeviceLayer::PlatformMgr().ScheduleWork(reportStateOnMatterThread, 0);
    if (error != CHIP_NO_ERROR) {
      ESP_LOGW("matter", "Unable to schedule thermostat report: %" CHIP_ERROR_FORMAT, error.Format());
      portENTER_CRITICAL(&sStateMutex);
      sState.pendingReport = false;
      portEXIT_CRITICAL(&sStateMutex);
    }
  }
}

esp_err_t attributeUpdateCallback(
    esp_matter::attribute::callback_type_t type,
    uint16_t endpointId,
    uint32_t clusterId,
    uint32_t attributeId,
    esp_matter_attr_val_t *value,
    void *) {
  if (type != esp_matter::attribute::PRE_UPDATE || endpointId != sEndpointId || sHvac == nullptr) {
    return ESP_OK;
  }

  ThermostatState state;
  portENTER_CRITICAL(&sStateMutex);
  state = sState;
  portEXIT_CRITICAL(&sStateMutex);

  if (clusterId == FanControl::Id && attributeId == FanControl::Attributes::FanMode::Id) {
    if (value->val.u8 == static_cast<uint8_t>(FanControl::FanModeEnum::kOff)) {
      sHvac->queueCommand(hvac::Mode::Off, state.coolingSetpointC);
    } else {
      sHvac->queueFanSpeed(toHvacFanSpeed(value->val.u8));
    }
    return ESP_OK;
  }

  if (clusterId == FanControl::Id && attributeId == FanControl::Attributes::RockSetting::Id) {
    sHvac->queueSwing(toHvacSwing(value->val.u8));
    return ESP_OK;
  }

  if (clusterId == OnOff::Id && attributeId == OnOff::Attributes::OnOff::Id) {
    sHvac->queueCommand(value->val.b ? hvac::Mode::Auto : hvac::Mode::Off, state.coolingSetpointC);
    return ESP_OK;
  }

  if (clusterId != Thermostat::Id) {
    return ESP_OK;
  }

  if (attributeId == Thermostat::Attributes::SystemMode::Id) {
    state.mode = toHvacMode(value->val.u8);
  } else if (attributeId == Thermostat::Attributes::OccupiedHeatingSetpoint::Id) {
    state.heatingSetpointC = value->val.i16 / 100.0f;
  } else if (attributeId == Thermostat::Attributes::OccupiedCoolingSetpoint::Id) {
    state.coolingSetpointC = value->val.i16 / 100.0f;
  } else {
    return ESP_OK;
  }

  // Matter may write SystemMode and a setpoint as separate transactions. Keep
  // the requested values here before queueing the command so a following write
  // cannot fall back to the previous (or startup-default) setpoint.
  portENTER_CRITICAL(&sStateMutex);
  sState.mode = state.mode;
  sState.heatingSetpointC = state.heatingSetpointC;
  sState.coolingSetpointC = state.coolingSetpointC;
  portEXIT_CRITICAL(&sStateMutex);

  float target = 23.0f;
  if (state.mode == hvac::Mode::Heat) {
    target = state.heatingSetpointC;
  } else if (state.mode == hvac::Mode::Cool) {
    target = state.coolingSetpointC;
  } else if (state.mode == hvac::Mode::Auto) {
    target = (state.heatingSetpointC + state.coolingSetpointC) / 2.0f;
  }
  target = std::clamp(target, kMinTargetC, kMaxTargetC);
  ESP_LOGI("matter", "Thermostat request: mode=%u heat=%.2f C cool=%.2f C; sending %.2f C",
           static_cast<unsigned>(toMatterMode(state.mode)), state.heatingSetpointC,
           state.coolingSetpointC, target);
  sHvac->queueCommand(state.mode, target);
  return ESP_OK;
}

void configureMetadata() {
  esp_matter_attr_val_t value = esp_matter_char_str(const_cast<char *>(kManufacturer), strlen(kManufacturer));
  esp_matter::attribute::update(
      0, BasicInformation::Id, BasicInformation::Attributes::VendorName::Id, &value);
  value = esp_matter_char_str(const_cast<char *>(kProductName), strlen(kProductName));
  esp_matter::attribute::update(
      0, BasicInformation::Id, BasicInformation::Attributes::ProductName::Id, &value);
  value = esp_matter_char_str(const_cast<char *>(kModel), strlen(kModel));
  esp_matter::attribute::update(
      0, BasicInformation::Id, BasicInformation::Attributes::ProductLabel::Id, &value);
  value = esp_matter_char_str(const_cast<char *>(kProductName), strlen(kProductName));
  esp_matter::attribute::update(
      0, BasicInformation::Id, BasicInformation::Attributes::NodeLabel::Id, &value);
}

chip::RendezvousInformationFlags rendezvousFlags() {
  chip::RendezvousInformationFlags flags;
  flags.Set(chip::RendezvousInformationFlag::kBLE);
  flags.Set(chip::RendezvousInformationFlag::kOnNetwork);
  return flags;
}

}  // namespace

bool begin(hvac::MideaHvac &hvac) {
  sHvac = &hvac;

  esp_matter::node::config_t nodeConfig;
  esp_matter::node_t *node = esp_matter::node::create(&nodeConfig, attributeUpdateCallback, nullptr, nullptr);
  if (node == nullptr) {
    ESP_LOGE("matter", "Failed to create Matter node");
    return false;
  }

  esp_matter::endpoint::room_air_conditioner::config_t config;
  config.on_off.on_off = false;
  config.thermostat.local_temperature = nullable<int16_t>(2000);
  config.thermostat.control_sequence_of_operation =
      static_cast<uint8_t>(Thermostat::ControlSequenceOfOperationEnum::kCoolingAndHeating);
  config.thermostat.system_mode = static_cast<uint8_t>(Thermostat::SystemModeEnum::kOff);
  config.thermostat.feature_flags = esp_matter::cluster::thermostat::feature::auto_mode::get_id();
  config.thermostat.features.auto_mode.min_setpoint_dead_band = 25;
  config.thermostat.features.heating.occupied_heating_setpoint = 2100;
  config.thermostat.features.cooling.occupied_cooling_setpoint = 2400;

  esp_matter::endpoint_t *endpoint = esp_matter::endpoint::room_air_conditioner::create(
      node, &config, esp_matter::ENDPOINT_FLAG_NONE, nullptr);
  if (endpoint == nullptr) {
    ESP_LOGE("matter", "Failed to create room air conditioner endpoint");
    return false;
  }

  esp_matter::cluster::fan_control::config_t fanConfig;
  fanConfig.fan_mode = static_cast<uint8_t>(FanControl::FanModeEnum::kAuto);
  fanConfig.fan_mode_sequence = static_cast<uint8_t>(FanControl::FanModeSequenceEnum::kOffLowMedHighAuto);
  esp_matter::cluster_t *fanCluster = esp_matter::cluster::fan_control::create(
      endpoint, &fanConfig, esp_matter::CLUSTER_FLAG_SERVER);
  if (fanCluster == nullptr) {
    ESP_LOGE("matter", "Failed to create fan control cluster");
    return false;
  }
  esp_matter::cluster::fan_control::feature::fan_auto::add(fanCluster);
  esp_matter::cluster::fan_control::feature::rocking::config_t rockingConfig;
  rockingConfig.rock_support = toMatterRockSetting(hvac::Swing::Both);
  rockingConfig.rock_setting = 0;
  esp_matter::cluster::fan_control::feature::rocking::add(fanCluster, &rockingConfig);
  sEndpointId = esp_matter::endpoint::get_id(endpoint);
  configureMetadata();
  return true;
}

bool start() {
  const esp_err_t error = esp_matter::start(nullptr);
  if (error != ESP_OK) {
    ESP_LOGE("matter", "Failed to start Matter: %s", esp_err_to_name(error));
    return false;
  }
  return true;
}

void publishHvacState(const hvac::State &state) {
  if (state.indoorTemperatureC < -20.0f || state.indoorTemperatureC > 70.0f ||
      state.targetTemperatureC < kMinTargetC || state.targetTemperatureC > kMaxTargetC) {
    return;
  }

  ESP_LOGI("matter", "Midea state: room=%.1f C target=%.1f C mode=%u", state.indoorTemperatureC,
           state.targetTemperatureC, static_cast<unsigned>(toMatterMode(state.mode)));

  ThermostatState updated;
  portENTER_CRITICAL(&sStateMutex);
  sState.mode = state.mode;
  sState.fanSpeed = state.fanSpeed;
  sState.swing = state.swing;
  sState.indoorTemperatureC = state.indoorTemperatureC;
  if (state.mode == hvac::Mode::Heat) {
    sState.heatingSetpointC = state.targetTemperatureC;
  } else if (state.mode == hvac::Mode::Cool) {
    sState.coolingSetpointC = state.targetTemperatureC;
  } else if (state.mode == hvac::Mode::Auto) {
    const float center = std::clamp(
        state.targetTemperatureC,
        kMinTargetC + kAutoDeadbandC / 2.0f,
        kMaxTargetC - kAutoDeadbandC / 2.0f);
    sState.heatingSetpointC = center - kAutoDeadbandC / 2.0f;
    sState.coolingSetpointC = center + kAutoDeadbandC / 2.0f;
  }
  portEXIT_CRITICAL(&sStateMutex);
  scheduleStateReport();
}

void printCommissioningInfo() {
  char qrPayload[128] = {};
  chip::MutableCharSpan qrSpan(qrPayload);
  char manualCode[32] = {};
  chip::MutableCharSpan manualSpan(manualCode);

  const CHIP_ERROR qrError = GetQRCode(qrSpan, rendezvousFlags());
  const CHIP_ERROR manualError = GetManualPairingCode(manualSpan, rendezvousFlags());
  if (qrError != CHIP_NO_ERROR || manualError != CHIP_NO_ERROR) {
    ESP_LOGW("matter", "Unable to generate Matter onboarding codes");
    return;
  }

  printf("\nMidea UART Matter controller is ready for commissioning.\n");
  printf("Manual pairing code: %s\n", manualCode);
  printf("QR payload: %s\n", qrPayload);
  printf("QR code URL: https://project-chip.github.io/connectedhomeip/qrcode.html?data=%s\n", qrPayload);
}

}  // namespace matter_thermostat
