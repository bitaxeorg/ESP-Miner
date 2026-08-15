idf_component_register(SRCS "../../../main/http_server/websocket.c"
                       INCLUDE_DIRS "../../../main" "../../../main/http_server"
                       REQUIRES esp_http_server espressif__cjson)
target_compile_definitions(${COMPONENT_LIB} PRIVATE UNIT_TESTING=1)
