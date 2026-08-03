idf_component_register(
SRCS 
    "bm1370.c"
    "bm1368.c"
    "bm1366.c"
    "bm1397.c"
    "serial.c"
    "crc.c"
    "asic_common.c"
    "asic.c"
    "frequency_transition_bmXX.c"
    "pll.c"

INCLUDE_DIRS 
    "include"

PRIV_INCLUDE_DIRS
    "../../main"
    "../../main/tasks"

REQUIRES 
    "freertos"
    "driver"
    "stratum"
    "stratum_v2"
    "tcp_transport"
)
