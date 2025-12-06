idf_component_register(
SRCS
    "utils.c"
    "mining.c"
    "stratum_api.c"
                    
INCLUDE_DIRS
    "include"

REQUIRES
    "mbedtls"
    "app_update"
    "esp_timer"
)

# ESP-IDF 6.0 renamed json to cjson
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    idf_component_get_property(json_lib cjson COMPONENT_LIB)
    target_link_libraries(${COMPONENT_LIB} PUBLIC ${json_lib})
else()
    idf_component_get_property(json_lib json COMPONENT_LIB)
    target_link_libraries(${COMPONENT_LIB} PUBLIC ${json_lib})
endif()