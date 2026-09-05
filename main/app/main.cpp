#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <nvs_flash.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "hvac/midea_hvac.h"
#include "matter/matter_thermostat.h"
#include "debug/web_log.h"

extern "C" void app_main() {
  esp_err_t error = nvs_flash_init();
  if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    error = nvs_flash_init();
  }
  ESP_ERROR_CHECK(error);
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  web_log::begin();

  hvac::MideaHvac hvac;
  if (!matter_thermostat::begin(hvac)) {
    ESP_LOGE("app", "Matter endpoint setup failed");
    return;
  }

  hvac.setStateCallback(matter_thermostat::publishHvacState);
  hvac.begin();

  if (!matter_thermostat::start()) {
    ESP_LOGE("app", "Matter stack failed to start");
    return;
  }
  matter_thermostat::printCommissioningInfo();

  while (true) {
    hvac.poll();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
