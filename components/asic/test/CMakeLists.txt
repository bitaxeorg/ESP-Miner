idf_component_register(SRC_DIRS "."
                       INCLUDE_DIRS "." "../private_include" "../../stratum/test" "stubs" "../../../main"
                                    "../../../main/tasks" "../../../main/self_test"
                       REQUIRES cmock stratum stratum_v2 asic)
