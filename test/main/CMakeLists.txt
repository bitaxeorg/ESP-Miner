idf_component_register(SRCS "unit_test_all.c" "test_websocket.c"
                    INCLUDE_DIRS "." "../../main" "../../main/http_server"
                    REQUIRES main unity esp_http_server)
