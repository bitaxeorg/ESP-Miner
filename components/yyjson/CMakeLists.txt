idf_component_register(
    SRCS "yyjson/src/yyjson.c"
    INCLUDE_DIRS "yyjson/src"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-maybe-uninitialized -Wno-unused-function -Wno-unused-parameter)
