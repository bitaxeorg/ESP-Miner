idf_component_register(
SRCS
    "connect.c"

INCLUDE_DIRS 
    "include"
    "../../main"

REQUIRES
    "nvs_flash"
    "esp_wifi"
    "esp_event"
)