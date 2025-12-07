idf_component_register(
SRCS 
    "bm1370.c"
    "bm1368.c"
    "bm1366.c"
    "bm1397.c"
    "serial.c"
    "crc.c"
    "common.c"
    "asic.c"
    "frequency_transition_bmXX.c"
    "pll.c"

INCLUDE_DIRS 
    "include"

REQUIRES
    "freertos"
    "stratum"
)

# ESP-IDF 6.0+ uses separate driver components, 5.x uses unified "driver"
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    idf_component_get_property(uart_lib esp_driver_uart COMPONENT_LIB)
    target_link_libraries(${COMPONENT_LIB} PUBLIC ${uart_lib})
else()
    idf_component_get_property(driver_lib driver COMPONENT_LIB)
    target_link_libraries(${COMPONENT_LIB} PUBLIC ${driver_lib})
endif()


# Include the header files from "main" directory
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../main")

# Include the header files from "main/tasks" directory
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../main/tasks")
