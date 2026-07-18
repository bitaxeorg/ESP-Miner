idf_component_register(
SRCS
    "http_auth.c"

INCLUDE_DIRS
    "include"

REQUIRES
    "esp_http_server"
    "mbedtls"
    "nvs_flash"
    "esp_hw_support"
)
