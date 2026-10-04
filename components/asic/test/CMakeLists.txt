idf_component_register(SRC_DIRS "."
                       INCLUDE_DIRS "." "../../stratum/test" "stubs"
                                    "../../../main/tasks" "../../../main/thermal" "../../../main/power" "../../../main/self_test" "../../../main"
                       PRIV_INCLUDE_DIRS "../private_include"
                       REQUIRES cmock stratum stratum_v2 asic)

target_sources(${COMPONENT_LIB} PRIVATE "../../../main/power/bonanza_power_policy.c")

# Compile each public BZM header using only the ASIC component's exported
# dependencies. These targets must not inherit this test component's private
# headers or hardware stubs.
foreach(header board bridge chain driver frequency lease_guard power runtime_health telemetry)
    add_library(bzm_public_header_${header} OBJECT "public_headers/check.c")
    target_link_libraries(bzm_public_header_${header} PRIVATE idf::asic)
    target_compile_definitions(bzm_public_header_${header} PRIVATE
        BZM_PUBLIC_HEADER="bzm/${header}.h")
endforeach()
