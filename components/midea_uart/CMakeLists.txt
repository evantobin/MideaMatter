file(GLOB_RECURSE MIDEA_UART_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../../libraries/MideaUART/src/*.cpp")

idf_component_register(
    SRCS ${MIDEA_UART_SOURCES}
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/../../libraries/MideaUART/include"
        "${CMAKE_CURRENT_LIST_DIR}/../../libraries/MideaUART/src"
    REQUIRES esp_netif esp_timer esp_wifi
)
