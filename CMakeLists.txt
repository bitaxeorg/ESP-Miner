# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# (Not part of the boilerplate)
# This example uses an extra component for common functions such as Wi-Fi and Ethernet connection.
# set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/common_components/protocol_examples_common)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)


project(esp-miner)

# Native tests use an independent CMake build under build/host. Keeping this as
# an explicit target avoids adding host-toolchain requirements to normal
# firmware build and flash commands.
add_custom_target(host-test
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_host_tests.sh" --all
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
    VERBATIM
)

# Coverage uses a separate GCC build because sanitizer and gcov
# instrumentation serve different validation purposes.
add_custom_target(host-coverage
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_host_coverage.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
    VERBATIM
)
