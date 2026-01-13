idf_component_register(
SRCS
    "connect.c"
    "connect_wifi.c"
    "usb_net.c"

INCLUDE_DIRS 
    "include"
    "../../main"
    "../../main/tasks"
    "../asic/include"

REQUIRES
    "nvs_flash"
    "esp_wifi"
    "esp_event"
    "stratum"
    "esp_eth"
    "esp_tinyusb"
)
