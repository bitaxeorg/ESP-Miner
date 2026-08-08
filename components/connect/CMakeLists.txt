idf_component_register(
SRCS
    "connect.c"
    "usb_net.c"

INCLUDE_DIRS 
    "include"

PRIV_INCLUDE_DIRS
    "../../main"
    "../../main/tasks"

REQUIRES
    "asic"
    "nvs_flash"
    "esp_wifi"
    "esp_event"
    "stratum"
    "esp_eth"
    "esp_tinyusb"
    "stratum_v2"
    "espressif__mdns"
)
