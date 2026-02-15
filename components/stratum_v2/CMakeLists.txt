idf_component_register(
    SRCS "sv2_protocol.c" "sv2_noise.c"
    INCLUDE_DIRS "include"
    REQUIRES "mbedtls" "libsecp256k1" "tcp_transport"
)
