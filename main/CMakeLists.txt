idf_component_register(
    SRCS
        "app/main.cpp"
        "debug/web_log.cpp"
        "hvac/midea_hvac.cpp"
        "matter/matter_thermostat.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_event
        esp_http_server
        esp_matter
        esp_netif
        esp_timer
        freertos
        midea_uart
        nvs_flash
)
