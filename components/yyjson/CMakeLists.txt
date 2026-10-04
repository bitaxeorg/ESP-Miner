idf_component_register(
    SRCS "yyjson.c"
    INCLUDE_DIRS "include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-maybe-uninitialized -Wno-unused-function -Wno-unused-parameter)
