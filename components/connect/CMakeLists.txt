idf_component_register(
SRCS
    "connect.c"

INCLUDE_DIRS 
    "include"

REQUIRES
    "asic"
    "nvs_flash"
    "esp_wifi"
    "esp_event"
    "stratum"
    "stratum_v2"
    "espressif__mdns"
)

target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../main"
    "${CMAKE_CURRENT_LIST_DIR}/../../main/tasks"
)
