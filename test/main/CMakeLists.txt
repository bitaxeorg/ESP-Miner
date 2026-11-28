idf_component_register(SRCS "unit_test_all.c" "test_ota_security.c" "test_headless_boot.c"
                    INCLUDE_DIRS "."
                    PRIV_INCLUDE_DIRS "../../main" "../../main/tasks"
                    WHOLE_ARCHIVE)
