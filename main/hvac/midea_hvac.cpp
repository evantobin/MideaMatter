#include "hvac/midea_hvac.h"

#include <algorithm>

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

#include <MideaUART.h>

namespace hvac {
namespace {

constexpr const char *kLogTag = "hvac";

constexpr uart_port_t kUart = UART_NUM_1;
constexpr gpio_num_t kRxPin = GPIO_NUM_17;
constexpr gpio_num_t kTxPin = GPIO_NUM_16;
constexpr float kMinTargetC = 17.0f;
constexpr float kMaxTargetC = 30.0f;

using namespace dudanov::midea::ac;

class UartStream final : public dudanov::Stream {
 public:
  int available() override {
    size_t buffered = 0;
    uart_get_buffered_data_len(kUart, &buffered);
    return static_cast<int>(buffered);
  }

  int read() override {
    uint8_t byte = 0;
    return uart_read_bytes(kUart, &byte, 1, 0) == 1 ? byte : -1;
  }

  int peek() override { return -1; }

  size_t write(uint8_t byte) override {
    return uart_write_bytes(kUart, &byte, 1) == 1 ? 1 : 0;
  }

  size_t write(const uint8_t *data, size_t length) override {
    const int written = uart_write_bytes(kUart, data, length);
    return written > 0 ? static_cast<size_t>(written) : 0;
  }

  void flush() override { uart_wait_tx_done(kUart, pdMS_TO_TICKS(100)); }
};

UartStream sUartStream;
AirConditioner sAirConditioner;
MideaHvac *sOwner = nullptr;

Mode toAppMode(dudanov::midea::ac::Mode mode) {
  switch (mode) {
    case MODE_HEAT: return Mode::Heat;
    case MODE_COOL: return Mode::Cool;
    case MODE_AUTO: return Mode::Auto;
    case MODE_DRY: return Mode::Dry;
    case MODE_FAN_ONLY: return Mode::FanOnly;
    default: return Mode::Off;
  }
}

dudanov::midea::ac::Mode toMideaMode(Mode mode) {
  switch (mode) {
    case Mode::Heat: return MODE_HEAT;
    case Mode::Cool: return MODE_COOL;
    case Mode::Auto: return MODE_AUTO;
    case Mode::Dry: return MODE_DRY;
    case Mode::FanOnly: return MODE_FAN_ONLY;
    default: return MODE_OFF;
  }
}

FanSpeed toAppFanSpeed(dudanov::midea::ac::FanMode fanMode) {
  switch (fanMode) {
    case FAN_LOW:
    case FAN_SILENT: return FanSpeed::Low;
    case FAN_MEDIUM: return FanSpeed::Medium;
    case FAN_HIGH:
    case FAN_TURBO: return FanSpeed::High;
    default: return FanSpeed::Auto;
  }
}

dudanov::midea::ac::FanMode toMideaFanSpeed(FanSpeed fanSpeed) {
  switch (fanSpeed) {
    case FanSpeed::Low: return FAN_LOW;
    case FanSpeed::Medium: return FAN_MEDIUM;
    case FanSpeed::High: return FAN_HIGH;
    default: return FAN_AUTO;
  }
}

Swing toAppSwing(dudanov::midea::ac::SwingMode swingMode) {
  switch (swingMode) {
    case SWING_VERTICAL: return Swing::Vertical;
    case SWING_HORIZONTAL: return Swing::Horizontal;
    case SWING_BOTH: return Swing::Both;
    default: return Swing::Off;
  }
}

dudanov::midea::ac::SwingMode toMideaSwing(Swing swing) {
  switch (swing) {
    case Swing::Vertical: return SWING_VERTICAL;
    case Swing::Horizontal: return SWING_HORIZONTAL;
    case Swing::Both: return SWING_BOTH;
    default: return SWING_OFF;
  }
}

struct PendingCommand {
  bool pending = false;
  bool thermostatUpdate = false;
  Mode mode = Mode::Off;
  float targetTemperatureC = 23.0f;
  bool fanSpeedUpdate = false;
  FanSpeed fanSpeed = FanSpeed::Auto;
  bool swingUpdate = false;
  Swing swing = Swing::Off;
};

PendingCommand sPendingCommand;
portMUX_TYPE sCommandMutex = portMUX_INITIALIZER_UNLOCKED;

void onMideaStateChanged() {
  if (sOwner != nullptr) {
    sOwner->publishState();
  }
}

}  // namespace

void MideaHvac::begin() {
  uart_config_t config = {};
  config.baud_rate = 9600;
  config.data_bits = UART_DATA_8_BITS;
  config.parity = UART_PARITY_DISABLE;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;
  ESP_ERROR_CHECK(uart_param_config(kUart, &config));
  ESP_ERROR_CHECK(uart_set_pin(kUart, kTxPin, kRxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  ESP_ERROR_CHECK(uart_driver_install(kUart, 2048, 2048, 0, nullptr, 0));

  sOwner = this;
  sAirConditioner.setStream(&sUartStream);
  sAirConditioner.setPeriod(1000);
  sAirConditioner.setTimeout(2000);
  sAirConditioner.setNumAttempts(3);
  sAirConditioner.setBeeper(false);
  sAirConditioner.addOnStateCallback(onMideaStateChanged);
  sAirConditioner.setup();
}

void MideaHvac::poll() {
  PendingCommand command;
  portENTER_CRITICAL(&sCommandMutex);
  command = sPendingCommand;
  sPendingCommand = PendingCommand{};
  portEXIT_CRITICAL(&sCommandMutex);

  if (command.pending) {
    ESP_LOGI(kLogTag, "Sending Midea command: thermostat=%d fan=%d swing=%d",
             command.thermostatUpdate, command.fanSpeedUpdate, command.swingUpdate);
    Control control;
    if (command.thermostatUpdate) {
      control.mode = toMideaMode(command.mode);
      control.targetTemp = std::clamp(command.targetTemperatureC, kMinTargetC, kMaxTargetC);
    }
    if (command.fanSpeedUpdate) {
      control.fanMode = toMideaFanSpeed(command.fanSpeed);
    }
    if (command.swingUpdate) {
      control.swingMode = toMideaSwing(command.swing);
    }
    sAirConditioner.control(control);
  }

  sAirConditioner.loop();
}

void MideaHvac::queueCommand(Mode mode, float targetTemperatureC) {
  ESP_LOGI(kLogTag, "Queued mode %d at %.1f C", static_cast<int>(mode), targetTemperatureC);
  portENTER_CRITICAL(&sCommandMutex);
  sPendingCommand.pending = true;
  sPendingCommand.thermostatUpdate = true;
  sPendingCommand.mode = mode;
  sPendingCommand.targetTemperatureC = std::clamp(targetTemperatureC, kMinTargetC, kMaxTargetC);
  portEXIT_CRITICAL(&sCommandMutex);
}

void MideaHvac::queueFanSpeed(FanSpeed fanSpeed) {
  ESP_LOGI(kLogTag, "Queued fan speed %d", static_cast<int>(fanSpeed));
  portENTER_CRITICAL(&sCommandMutex);
  sPendingCommand.pending = true;
  sPendingCommand.fanSpeedUpdate = true;
  sPendingCommand.fanSpeed = fanSpeed;
  portEXIT_CRITICAL(&sCommandMutex);
}

void MideaHvac::queueSwing(Swing swing) {
  ESP_LOGI(kLogTag, "Queued swing %d", static_cast<int>(swing));
  portENTER_CRITICAL(&sCommandMutex);
  sPendingCommand.pending = true;
  sPendingCommand.swingUpdate = true;
  sPendingCommand.swing = swing;
  portEXIT_CRITICAL(&sCommandMutex);
}

void MideaHvac::setStateCallback(StateCallback callback) {
  stateCallback_ = std::move(callback);
}

void MideaHvac::publishState() {
  if (!stateCallback_) {
    return;
  }
  State state;
  state.mode = toAppMode(sAirConditioner.getMode());
  state.fanSpeed = toAppFanSpeed(sAirConditioner.getFanMode());
  state.swing = toAppSwing(sAirConditioner.getSwingMode());
  state.indoorTemperatureC = sAirConditioner.getIndoorTemp();
  state.targetTemperatureC = sAirConditioner.getTargetTemp();
  stateCallback_(state);
}

}  // namespace hvac
