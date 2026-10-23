idf_component_register(SRC_DIRS "." "bzm"
                       INCLUDE_DIRS "." "../../stratum/test" "stubs"
                                    "../../../main/tasks" "../../../main/thermal" "../../../main/power" "../../../main/self_test" "../../../main"
                       PRIV_INCLUDE_DIRS "../private_include"
                       REQUIRES cmock stratum stratum_v2 asic
                       PRIV_REQUIRES esp_timer esp_psram esp_driver_gpio esp_driver_ledc esp_adc heap)

# Compile each public BZM header using only the ASIC component's exported
# dependencies. These targets must not inherit this test component's private
# headers or hardware stubs.
foreach(header board control chain driver frequency power telemetry)
    add_library(bzm_public_header_${header} OBJECT "bzm/public_headers/check.c")
    target_link_libraries(bzm_public_header_${header} PRIVATE idf::asic)
    target_compile_definitions(bzm_public_header_${header} PRIVATE
        BZM_PUBLIC_HEADER="bzm/${header}.h")
endforeach()
