# gateway/platform/esp32/CMakeLists.txt
#
# ESP-IDF component that bundles the portable gateway core with the
# ESP32 platform implementation.
#
# Add to the firmware build by inserting the following line in the
# root CMakeLists.txt BEFORE the project() call:
#
#   set(EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/gateway/platform/esp32")
#
# The main component (main/) must:
#   - Add "gateway" to its PRIV_REQUIRES list

idf_component_register(
    SRCS
        # Portable gateway core (no ESP-IDF dependencies)
        "../../src/gateway_core.c"
        "../../src/wslink.c"

        # ESP32 platform HAL implementation
        "gateway_platform_esp32.c"

    INCLUDE_DIRS
        # Exposes gateway_types.h, gateway_core.h, gateway_platform.h
        "../../include"
        "."

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
