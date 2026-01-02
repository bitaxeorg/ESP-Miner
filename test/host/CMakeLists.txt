# CMakeLists.txt for host-based unit testing
# Run with: cmake -B build && cmake --build build && ./build/run_tests

cmake_minimum_required(VERSION 3.16)
project(esp_miner_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic)

# Enable AddressSanitizer in debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# ============================================
# Unity Test Framework
# ============================================
include(FetchContent)
FetchContent_Declare(
    unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.6.0
)
FetchContent_MakeAvailable(unity)

# ============================================
# Include Paths
# ============================================
set(STUB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
set(MOCK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mocks)
set(FAKE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fakes)
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(MAIN_DIR ${PROJECT_ROOT}/main)
set(COMPONENTS_DIR ${PROJECT_ROOT}/components)

# Include directories for host compilation
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${STUB_DIR}
    ${MOCK_DIR}
    ${FAKE_DIR}
    # Project headers (selectively - avoid hardware-dependent ones)
    ${MAIN_DIR}
    ${MAIN_DIR}/tasks
    ${COMPONENTS_DIR}/stratum/include
    ${COMPONENTS_DIR}/asic/include
)

# ============================================
# Stub and Mock Libraries
# ============================================
add_library(esp_stubs STATIC
    ${STUB_DIR}/esp_err.c
)
target_include_directories(esp_stubs PUBLIC ${STUB_DIR})

add_library(mock_nvs STATIC
    ${MOCK_DIR}/mock_nvs.c
)
target_include_directories(mock_nvs PUBLIC ${MOCK_DIR} ${STUB_DIR})

add_library(fakes STATIC
    ${FAKE_DIR}/fake_time.c
)
target_include_directories(fakes PUBLIC ${FAKE_DIR})

# ============================================
# Test Helper Library
# ============================================
add_library(test_helpers INTERFACE)
target_include_directories(test_helpers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${STUB_DIR}
    ${MOCK_DIR}
    ${FAKE_DIR}
)
target_link_libraries(test_helpers INTERFACE
    unity
    esp_stubs
    mock_nvs
    fakes
)

# ============================================
# Modules Under Test
# ============================================
# Note: We compile production code here with stubs
# Each module should be added as needed

# Stratum utilities require mbedtls - will be added when we have mbedtls stub
# For now, modules will be added incrementally as their dependencies are stubbed
# Production code will be compiled and tested as we refactor in later phases

# ============================================
# Test Executables
# ============================================

# Example test file (to verify infrastructure works)
add_executable(test_infrastructure
    test_infrastructure.c
)
target_link_libraries(test_infrastructure
    test_helpers
    pthread
)

# Test runner that runs all tests
add_executable(run_tests
    test_main.c
    test_infrastructure.c
    # Add more test files here as they are created:
    # test_autotune.c
    # test_fan_control.c
    # etc.
)
target_link_libraries(run_tests
    test_helpers
    pthread
)

# ============================================
# CTest Integration
# ============================================
enable_testing()
add_test(NAME infrastructure_tests COMMAND test_infrastructure)
add_test(NAME all_tests COMMAND run_tests)

# ============================================
# Coverage (optional, requires gcov)
# ============================================
option(ENABLE_COVERAGE "Enable code coverage" OFF)
if(ENABLE_COVERAGE)
    add_compile_options(--coverage)
    add_link_options(--coverage)
endif()

# ============================================
# Print configuration
# ============================================
message(STATUS "Host test configuration:")
message(STATUS "  Project root: ${PROJECT_ROOT}")
message(STATUS "  Stub dir: ${STUB_DIR}")
message(STATUS "  Mock dir: ${MOCK_DIR}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
