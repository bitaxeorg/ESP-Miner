idf_component_register(SRC_DIRS "."
                       INCLUDE_DIRS "." "../private_include" "../../stratum/test" "../../stratum/test/stubs"
                                    "../../../main/tasks" "../../../main/self_test"
                       REQUIRES cmock stratum stratum_v2 asic)
