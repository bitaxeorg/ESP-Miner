# This is the project CMakeLists.txt file for the test subproject
cmake_minimum_required(VERSION 3.16)

# Include the components directory of the main application:
#
set(EXTRA_COMPONENT_DIRS "../components")

# Set the components to include the tests for.
# This can be overriden from CMake cache:
# - when invoking CMake directly: cmake -D TEST_COMPONENTS="xxxxx" ..
# - when using idf.py: idf.py -T xxxxx build
#
set(TEST_COMPONENTS "stratum stratum_v2 asic cjson_allocator" CACHE STRING "List of components to test")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(IDF_TARGET "esp32s3")

idf_build_set_property(COMPILE_DEFINITIONS "-DCONFIG_ASIC_FREQUENCY=100" APPEND)

project(unit_test_stratum)

option(ENABLE_UBSAN "Enable undefined behavior checks in unit tests" OFF)

if(ENABLE_UBSAN)
    # Instrument repository components and their tests without exhausting RAM
    # by also instrumenting ESP-IDF. Its UBSan runtime aborts on the first error.
    get_filename_component(components_dir "../components" ABSOLUTE)
    idf_build_get_property(build_components BUILD_COMPONENTS)
    foreach(component IN LISTS build_components)
        idf_component_get_property(component_dir ${component} COMPONENT_DIR)
        string(FIND "${component_dir}" "${components_dir}/" component_prefix)
        if(component_prefix EQUAL 0)
            idf_component_get_property(component_lib ${component} COMPONENT_LIB)
            target_compile_options(${component_lib} PRIVATE -fsanitize=undefined)
        endif()
    endforeach()
endif()
