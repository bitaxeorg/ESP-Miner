idf_component_register(SRCS "real_roots.c" INCLUDE_DIRS "."
                       PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}" REQUIRES mbedtls)
get_filename_component(repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(roots_file "${CMAKE_CURRENT_BINARY_DIR}/roots.inc")
add_custom_command(OUTPUT "${roots_file}"
    COMMAND "${PYTHON}" "${repo_root}/tools/generate_pinned_roots.py" --output "${roots_file}"
    DEPENDS "${repo_root}/tools/generate_pinned_roots.py"
            "${repo_root}/certificates/cacert-2026-10-20.pem"
    VERBATIM)
target_sources(${COMPONENT_LIB} PRIVATE "${roots_file}")
