idf_build_get_property(target IDF_TARGET)

set(COMPONENT_ADD_INCLUDEDIRS "${CMAKE_CURRENT_SOURCE_DIR}")
set(COMPONENT_SRCS "stb_png.c")

idf_component_register(
    INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}"
    SRCS "${COMPONENT_SRCS}"

    PRIV_REQUIRES
        "esp_lcd"
        "esp_lvgl_port"
)
