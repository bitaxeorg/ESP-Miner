idf_component_register(
SRCS
    "connect.c"

INCLUDE_DIRS
    "include"
    "../../main"
    "../../main/tasks"
    "../asic/include"
    "../../gateway/include"

REQUIRES
    "nvs_flash"
    "esp_wifi"
    "esp_event"
    "stratum"
)
