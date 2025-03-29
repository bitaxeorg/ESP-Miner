idf_component_register(
SRCS
    "influx.c"

INCLUDE_DIRS
    "include"

REQUIRES
    "freertos"
    "driver"
    "esp_http_client"
    "json"
)


# Include the header files from "main" directory
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../main")

# Include the header files from "main/tasks" directory
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../main/tasks")