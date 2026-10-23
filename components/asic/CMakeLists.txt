include("${CMAKE_CURRENT_LIST_DIR}/bzm/sources.cmake")

idf_component_register(
SRCS
    ${BZM_SOURCES}
    "bzm/port/esp/driver.c"
    "bzm/port/esp/platform.c"
    "bzm/port/esp/result.c"
    "bzm/port/esp/control.c"
    "bzm/port/esp/gpio.c"
    "bzm/port/esp/serial.c"

    "bm1373.c"
    "bm1370.c"
    "bm1368.c"
    "bm1366.c"
    "bm1397.c"
    "serial.c"
    "crc.c"
    "asic_common.c"
    "asic_job_slot.c"
    "asic.c"
    "asic_submit.c"
    "bm_hashrate.c"
    "bm_job_packet.c"
    "bm_job_midstate.c"
    "frequency_transition_bmXX.c"
    "pll.c"

INCLUDE_DIRS
    "include"

PRIV_INCLUDE_DIRS
    "private_include"

REQUIRES
    "mining_job"
    "freertos"
    "esp_driver_uart"
    "stratum"
    "stratum_v2"
    "tcp_transport"

PRIV_REQUIRES
    "pthread"
    "esp_timer"
    "esp_psram"
    "esp_driver_gpio"
    "esp_driver_ledc"
    "heap"
    "mbedtls"
    "esp_rom"
)

target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../main"
    "${CMAKE_CURRENT_LIST_DIR}/../../main/tasks"
)
