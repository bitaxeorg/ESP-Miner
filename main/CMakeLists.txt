idf_component_register(
SRCS
    "adc.c"
    "i2c_bitaxe.c"
    "main.c"
    "nvs_config.c"
    "display.c"
    "screen.c"
    "input.c"
    "system.c"
    "work_queue.c"
    "lv_font_portfolio-6x8.c"
    "logo.c"
    "./bap/bap.c"
    "./bap/bap_protocol.c"
    "./bap/bap_uart.c"
    "./bap/bap_handlers.c"
    "./bap/bap_subscription.c"
    "device_config.c"
    "./http_server/http_server.c"
    "./http_server/websocket.c"
    "./http_server/theme_api.c"
    "./http_server/axe-os/api/system/asic_settings.c"
    "./self_test/self_test.c"
    "./tasks/stratum_task.c"
    "./tasks/create_jobs_task.c"
    "./tasks/asic_result_task.c"
    "./tasks/power_management_task.c"
    "./tasks/statistics_task.c"
    "./tasks/hashrate_monitor_task.c"
    "./thermal/EMC2101.c"
    "./thermal/EMC2103.c"
    "./thermal/EMC2302.c"
    "./thermal/TMP1075.c"
    "./thermal/thermal.c"
    "./thermal/PID.c"
    "./power/TPS546.c"
    "./power/DS4432U.c"
    "./power/INA260.c"
    "./power/power.c"
    "./power/vcore.c"
    "./power/asic_reset.c"
    "./power/asic_init.c"

INCLUDE_DIRS
    "."
    "tasks"
    "http_server"
    "http_server/axe-os/api/system"
    "self_test"
    "bap"
    "../components/asic/include"
    "../components/connect/include"
    "../components/dns_server/include"
    "../components/stratum/include"
    "thermal"
    "power"

PRIV_REQUIRES
    "app_update"
    "driver"
    "esp_adc"
    "esp_app_format"
    "esp_event"
    "esp_http_server"
    "esp_netif"
    "esp_psram"
    "esp_timer"
    "esp_wifi"
    "json"
    "nvs_flash"
    "spiffs"
    "vfs"
    "esp_driver_i2c"
    "tcp_transport"

EMBED_FILES "http_server/recovery_page.html"
)

idf_build_set_property(COMPILE_OPTIONS "-DLV_CONF_INCLUDE_SIMPLE=1" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-DLV_CONF_PATH=\"${CMAKE_SOURCE_DIR}/main/lv_conf.h\"" APPEND)

set(WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/http_server/axe-os")

if("$ENV{GITHUB_ACTIONS}" STREQUAL "true")
    message(STATUS "Running on GitHub Actions. Web ui will be prebuilt.")

    spiffs_create_partition_image(www ${WEB_SRC_DIR}/dist/axe-os FLASH_IN_PROJECT)
else()
    find_program(NPM_EXECUTABLE npm)
    if(NOT NPM_EXECUTABLE AND NOT EXISTS ${WEB_SRC_DIR}/dist)
        message(FATAL_ERROR "npm is not found! Please install it to proceed.")
    endif()

    # OpenAPI generation - only when openapi.yaml changes
    set(OPENAPI_SPEC "${CMAKE_CURRENT_SOURCE_DIR}/http_server/openapi.yaml")
    set(OPENAPI_STAMP "${CMAKE_BINARY_DIR}/openapi_generate.stamp")

    add_custom_command(
        OUTPUT ${OPENAPI_STAMP}
        COMMAND ${NPM_EXECUTABLE} i
        COMMAND ${NPM_EXECUTABLE} run generate:api
        COMMAND ${CMAKE_COMMAND} -E touch ${OPENAPI_STAMP}
        WORKING_DIRECTORY ${WEB_SRC_DIR}
        DEPENDS ${OPENAPI_SPEC}
        COMMENT "Generating API client from OpenAPI spec (openapi.yaml changed)"
        VERBATIM
        USES_TERMINAL
    )

    # Collect all frontend source files to track dependencies
    file(GLOB_RECURSE WEB_UI_SOURCES
        "${WEB_SRC_DIR}/src/*.*"
    )

    # Also track build configuration files
    list(APPEND WEB_UI_SOURCES
        "${WEB_SRC_DIR}/package.json"
        "${WEB_SRC_DIR}/package-lock.json"
        "${WEB_SRC_DIR}/angular.json"
        "${WEB_SRC_DIR}/tsconfig.json"
        "${WEB_SRC_DIR}/tsconfig.app.json"
    )

    # Use a stamp file to track when the build was last done
    set(WEB_UI_STAMP "${CMAKE_BINARY_DIR}/web_ui_build.stamp")
    # Track version.txt which is generated last and doesn't get deleted
    set(WEB_UI_OUTPUT "${WEB_SRC_DIR}/dist/axe-os/version.txt")

    # Custom command that only runs when source files change
    add_custom_command(
        OUTPUT ${WEB_UI_STAMP} ${WEB_UI_OUTPUT}
        COMMAND ${NPM_EXECUTABLE} i
        COMMAND ${NPM_EXECUTABLE} run build:no-api-gen
        COMMAND ${CMAKE_COMMAND} -E touch ${WEB_UI_STAMP}
        WORKING_DIRECTORY ${WEB_SRC_DIR}
        DEPENDS ${WEB_UI_SOURCES} ${OPENAPI_STAMP}
        COMMENT "Building Web UI (source files changed)"
        VERBATIM
        USES_TERMINAL
    )

    # Create a target for the web UI build
    add_custom_target(web_ui_dist ALL
        DEPENDS ${WEB_UI_STAMP} ${OPENAPI_STAMP}
    )

    add_dependencies(${COMPONENT_LIB} web_ui_dist)

    spiffs_create_partition_image(www ${WEB_SRC_DIR}/dist/axe-os FLASH_IN_PROJECT DEPENDS web_ui_dist)
endif()
