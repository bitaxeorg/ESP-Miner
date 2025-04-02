idf_component_register(
SRCS
    "connect.c"

INCLUDE_DIRS 
    "include"
    "../asic/include"
    "../../main"
    "../../main/tasks"

REQUIRES
    "nvs_flash"
    "esp_wifi"
    "esp_event"
    "stratum"
)
