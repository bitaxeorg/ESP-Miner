idf_component_register(
SRCS
    "adc.c"
    "DS4432U.c"
    "EMC2101.c"
    "fonts.c"
    "INA260.c"
    "led_controller.c"
    "main.c"
    "nvs_config.c"
    "oled.c"
    "system.c"
    "work_queue.c"
    "./tasks/stratum_task.c"
    "./tasks/create_jobs_task.c"
    "./tasks/asic_task.c"
    "./tasks/asic_result_task.c"
    "./tasks/user_input_task.c"
    "./tasks/power_management_task.c"
    "./http_server/http_server.c"

INCLUDE_DIRS
    "."
    "tasks"
    "http_server"
    "../components/connect/include"
)

set(WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/http_server/axe-os")
if(EXISTS ${WEB_SRC_DIR}/dist/axe-os)
    spiffs_create_partition_image(www ${WEB_SRC_DIR}/dist/axe-os FLASH_IN_PROJECT)
else()
    message(WARNING "${WEB_SRC_DIR}/dist doesn't exit. Please run 'npm run build' in ${WEB_SRC_DIR}")
endif()