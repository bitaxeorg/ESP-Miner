idf_component_register(
SRCS
    "bzm_driver.c"
    "bzm_result.c"
    "bzm_transport.c"
    "bzm_bridge.c"
    "bzm_serial.c"

    "bzm.c"
    "bzm_balanced_ramp.c"
    "bzm_bringup.c"
    "bzm_dispatch_gate.c"
    "bzm_frame_parser.c"
    "bzm_frequency.c"
    "bzm_job_store.c"
    "bzm_lease_guard.c"
    "bzm_power.c"
    "bzm_reactor.c"
    "bzm_result_dedup.c"
    "bzm_runtime_health.c"
    "bzm_telemetry.c"
    "bzm_topology.c"
    "bzm_work.c"

    "bm1373.c"
    "bm1370.c"
    "bm1368.c"
    "bm1366.c"
    "bm1397.c"
    "serial.c"
    "crc.c"
    "asic_common.c"
    "asic.c"
    "asic_submit.c"
    "bm_hashrate.c"
    "bitmain_job_packet.c"
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
    "esp_timer"
    "esp_psram"
    "esp_driver_gpio"
    "heap"
)

target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../main"
    "${CMAKE_CURRENT_LIST_DIR}/../../main/tasks"
)
