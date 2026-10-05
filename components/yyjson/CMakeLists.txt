idf_component_register(
    SRCS "yyjson/src/yyjson.c"
         "yyjson_psram.c"
    INCLUDE_DIRS "include"
                 "yyjson/src"
    REQUIRES "heap"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-maybe-uninitialized -Wno-unused-function -Wno-unused-parameter)
