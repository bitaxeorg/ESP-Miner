get_filename_component(ESP_MINER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

idf_component_register(SRC_DIRS "." "bzm"
                       INCLUDE_DIRS "." "../../stratum/test" "stubs"
                                    "${ESP_MINER_ROOT}/main/tasks" "${ESP_MINER_ROOT}/main/thermal"
                                    "${ESP_MINER_ROOT}/main/power" "${ESP_MINER_ROOT}/main/self_test"
                                    "${ESP_MINER_ROOT}/main" "${ESP_MINER_ROOT}"
                       PRIV_INCLUDE_DIRS "../private_include"
                       REQUIRES cmock stratum stratum_v2 asic
                       PRIV_REQUIRES app_update connect esp_wifi esp_timer esp_psram esp_driver_gpio esp_driver_ledc esp_adc esp_lcd heap)

# Compile each public BZM header using only the ASIC component's exported
# dependencies. These targets must not inherit this test component's private
# headers or hardware stubs.
set(BZM_HEADER_CHECK_SOURCES)
foreach(header board control chain driver frequency power telemetry)
    set(check_source "${CMAKE_CURRENT_BINARY_DIR}/header-checks/${header}.c")
    configure_file("bzm/public_headers/check.c.in" "${check_source}" @ONLY)
    list(APPEND BZM_HEADER_CHECK_SOURCES "${check_source}")
endforeach()
add_library(bzm_public_headers OBJECT ${BZM_HEADER_CHECK_SOURCES})
target_link_libraries(bzm_public_headers PRIVATE idf::asic)
