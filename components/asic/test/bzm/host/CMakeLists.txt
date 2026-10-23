cmake_minimum_required(VERSION 3.16)
project(asic_bzm_tests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
find_package(Threads REQUIRED)
enable_testing()

get_filename_component(ASIC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
include("${ASIC_DIR}/bzm/sources.cmake")
add_library(bzm_test_support STATIC
    ${BZM_SOURCES}
    "${ASIC_DIR}/bzm/port/posix/platform.c"
    "${ASIC_DIR}/../mining_job/asic_job.c")
target_include_directories(bzm_test_support PUBLIC
    "${ASIC_DIR}/include"
    "${ASIC_DIR}/../mining_job/include"
    PRIVATE "${ASIC_DIR}/private_include")
target_link_libraries(bzm_test_support PUBLIC Threads::Threads m)
target_compile_options(bzm_test_support PRIVATE -Wall -Wextra -Werror -Wpedantic)

foreach(test transport manufacturing hash platform runtime_smoke driver_fault driver_backlog driver_schedule grouped_jobs memory)
    add_executable(bzm_test_${test} test_${test}.c)
    target_link_libraries(bzm_test_${test} PRIVATE bzm_test_support)
    target_include_directories(bzm_test_${test} PRIVATE "${ASIC_DIR}/private_include")
    target_compile_options(bzm_test_${test} PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME bzm_${test} COMMAND bzm_test_${test})
endforeach()

foreach(header balanced_ramp board bringup chain controls dispatch_gate
               frame_parser frequency platform posix power protocol registers
               runtime runtime_result status telemetry telemetry_decode topology
               transport work_registers)
    add_library(bzm_header_${header} OBJECT header.c)
    target_link_libraries(bzm_header_${header} PRIVATE bzm_test_support)
    target_compile_definitions(bzm_header_${header} PRIVATE
        BZM_PUBLIC_HEADER="bzm/${header}.h")
endforeach()
