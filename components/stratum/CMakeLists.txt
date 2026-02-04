idf_component_register(
SRCS
    "utils.c"
    "mining.c"
    "stratum_api.c"
    "coinbase_decoder.c"
    "segwit_addr.c"
    "base58.c"
                    
INCLUDE_DIRS
    "include"

REQUIRES
    "json"
    "mbedtls"
    "app_update"
    "esp_timer"
    "tcp_transport"
)
