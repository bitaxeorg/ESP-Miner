# components/gateway/CMakeLists.txt
#
# ESP-IDF component "gateway" — portable tRPC gateway core + ESP32 platform HAL.
#
# Source layout:
#   gateway/src/                  — portable C (no ESP-IDF dependencies)
#   gateway/platform/esp32/       — ESP32/ESP-IDF platform implementations
#   gateway/include/              — shared headers (gateway_types.h, etc.)
#
# The component name is "gateway" (derived from this directory's name).
# main/ declares PRIV_REQUIRES "gateway" to link against it.

idf_component_register(
    SRCS
        # Portable gateway core
        "../../gateway/src/gateway_core.c"
        "../../gateway/src/miner_adapter.c"

        # ESP32 platform HAL
        "../../gateway/platform/esp32/gateway_platform_esp32.c"

    INCLUDE_DIRS
        # Public: exposes gateway_types.h, gateway_core.h, gateway_platform.h,
        # miner_adapter.h, and gateway_platform_esp32.h to dependents
        "../../gateway/include"
        "../../gateway/platform/esp32"

    PRIV_REQUIRES
        "esp_http_client"
        "esp_netif"
        "esp_timer"
        "esp_websocket_client"
        "esp_psram"
        "freertos"
        "json"
        "lwip"
        "newlib"
)
