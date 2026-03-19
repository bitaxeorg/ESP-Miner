cmake_minimum_required(VERSION 3.16)
project(gateway C)

set(CMAKE_C_STANDARD 11)

# ── Dependencies via pkg-config ─────────────────────────────────────────────
find_package(PkgConfig REQUIRED)

pkg_check_modules(LWS     REQUIRED libwebsockets)
pkg_check_modules(CURL    REQUIRED libcurl)
pkg_check_modules(CJSON   REQUIRED libcjson)

# libwebsockets on Homebrew requires OpenSSL headers which are keg-only
find_package(OpenSSL REQUIRED)
if(APPLE)
    # Homebrew keg-only openssl is not in the default search path
    execute_process(
        COMMAND brew --prefix openssl
        OUTPUT_VARIABLE OPENSSL_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(OPENSSL_INCLUDE_DIR "${OPENSSL_PREFIX}/include")
endif()

# ── Sources ──────────────────────────────────────────────────────────────────
add_executable(gateway
    # Entry point
    gateway_main.c

    # macOS platform HAL
    gateway_platform_macos.c

    # Portable gateway core
    ../../src/gateway_core.c
    ../../src/miner_adapter.c
    ../../src/wslink.c
)

# ── Include dirs ─────────────────────────────────────────────────────────────
target_include_directories(gateway PRIVATE
    ../../include          # gateway_types.h, gateway_core.h, gateway_platform.h, miner_adapter.h
    ${LWS_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${CJSON_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

# ── Link ─────────────────────────────────────────────────────────────────────
target_link_libraries(gateway PRIVATE
    ${LWS_LINK_LIBRARIES}
    ${CURL_LINK_LIBRARIES}
    ${CJSON_LINK_LIBRARIES}
    pthread
)

target_compile_options(gateway PRIVATE
    ${LWS_CFLAGS_OTHER}
    ${CURL_CFLAGS_OTHER}
    ${CJSON_CFLAGS_OTHER}
    -Wall -Wextra -Wno-unused-parameter
)

# ── Install (optional) ───────────────────────────────────────────────────────
install(TARGETS gateway RUNTIME DESTINATION bin)
