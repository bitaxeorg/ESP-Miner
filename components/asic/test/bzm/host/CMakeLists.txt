cmake_minimum_required(VERSION 3.16)
project(asic_bzm_tests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
find_package(Threads REQUIRED)
enable_testing()

get_filename_component(ASIC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
get_filename_component(ESP_MINER_ROOT "${ASIC_DIR}/../.." ABSOLUTE)
set(SECP256K1_DIR "${ESP_MINER_ROOT}/components/libsecp256k1/libsecp256k1")
if(NOT EXISTS "${SECP256K1_DIR}/src/hash_impl.h")
    message(FATAL_ERROR "Initialize libsecp256k1: git submodule update --init components/libsecp256k1/libsecp256k1")
endif()
include("${ASIC_DIR}/bzm/sources.cmake")
add_library(bzm_test_support STATIC
    ${BZM_SOURCES}
    "${ASIC_DIR}/bm_job_midstate.c"
    "${ASIC_DIR}/bzm/port/posix/platform.c"
    "${ASIC_DIR}/../mining_job/asic_job.c")
target_include_directories(bzm_test_support PUBLIC
    "${ASIC_DIR}/include"
    "${ASIC_DIR}/../mining_job/include"
    PRIVATE "${ASIC_DIR}/private_include")
# The POSIX SHA256 callback uses internal headers from the pinned submodule.
target_include_directories(bzm_test_support SYSTEM PRIVATE "${SECP256K1_DIR}/src")
target_link_libraries(bzm_test_support PUBLIC Threads::Threads m)
target_compile_options(bzm_test_support PRIVATE -Wall -Wextra -Werror -Wpedantic)

foreach(test transport manufacturing hash platform driver_fault driver_backlog driver_schedule grouped_jobs memory)
    add_executable(bzm_test_${test} test_${test}.c)
    target_link_libraries(bzm_test_${test} PRIVATE bzm_test_support)
    target_include_directories(bzm_test_${test} PRIVATE "${ASIC_DIR}/private_include" "${ESP_MINER_ROOT}")
    target_compile_options(bzm_test_${test} PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME bzm_${test} COMMAND bzm_test_${test})
endforeach()

set(BZM_HEADER_CHECK_SOURCES)
foreach(header balanced_ramp board bringup chain controls dispatch_gate
               frame_parser frequency platform posix power protocol registers
               runtime runtime_result status telemetry telemetry_decode topology
               transport work_registers)
    set(check_source "${CMAKE_CURRENT_BINARY_DIR}/header-checks/${header}.c")
    configure_file("../public_headers/check.c.in" "${check_source}" @ONLY)
    list(APPEND BZM_HEADER_CHECK_SOURCES "${check_source}")
endforeach()
add_library(bzm_header_checks OBJECT ${BZM_HEADER_CHECK_SOURCES})
target_link_libraries(bzm_header_checks PRIVATE bzm_test_support)
target_compile_options(bzm_header_checks PRIVATE -Wall -Wextra -Werror -Wpedantic)
