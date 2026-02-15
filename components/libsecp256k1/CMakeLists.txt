idf_component_register(
    SRCS "libsecp256k1/src/secp256k1.c"
         "libsecp256k1/src/precomputed_ecmult.c"
         "libsecp256k1/src/precomputed_ecmult_gen.c"
    INCLUDE_DIRS "libsecp256k1/include"
    PRIV_INCLUDE_DIRS "libsecp256k1" "libsecp256k1/src"
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ECMULT_WINDOW_SIZE=4
    ECMULT_GEN_PREC_BITS=2
    ENABLE_MODULE_ELLSWIFT=1
    ENABLE_MODULE_SCHNORRSIG=1
    ENABLE_MODULE_EXTRAKEYS=1
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-parameter)
