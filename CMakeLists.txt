# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# (Not part of the boilerplate)
# This example uses an extra component for common functions such as Wi-Fi and Ethernet connection.
# set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/common_components/protocol_examples_common)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)


project(esp-miner)

# Existing sdkconfig files override sdkconfig.defaults, including revoked roots.
if(NOT CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
   OR NOT CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_NONE
   OR NOT CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE
   OR CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEPRECATED_LIST
   OR NOT CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH STREQUAL "certificates/cacert-2026-10-20.pem")
    message(FATAL_ERROR "ESP-Miner requires its pinned CA bundle. Update the certificate bundle settings in sdkconfig to match sdkconfig.defaults; see certificates/README.md.")
endif()

