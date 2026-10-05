#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "global_state.h"
#include "nvs_config.h"
#include "array.h"
#include "display.h"
#include "display_driver.h"
#include "display_oled.h"
#include "display_st7789.h"

static const char * TAG = "display";
static const char * LVGL_TAG = "lvgl";

static const DisplayDriver * active_driver = NULL;
static esp_lcd_panel_handle_t active_panel = NULL;
static bool display_state_on = false;

static void my_log_cb(lv_log_level_t level, const char * buf)
{
    switch (level) {
        case LV_LOG_LEVEL_TRACE:
            ESP_LOGV(LVGL_TAG, "%s", buf);
            break;
        case LV_LOG_LEVEL_INFO:
            ESP_LOGI(LVGL_TAG, "%s", buf);
            break;
        case LV_LOG_LEVEL_WARN:
            ESP_LOGW(LVGL_TAG, "%s", buf);
            break;
        case LV_LOG_LEVEL_ERROR:
            ESP_LOGE(LVGL_TAG, "%s", buf);
            break;
        case LV_LOG_LEVEL_USER:
            ESP_LOGI(LVGL_TAG, "%s", buf);
            break;
        case LV_LOG_LEVEL_NONE:
            break;
    }
}

static void display_apply_nvs_rotation(lv_disp_t * disp)
{
    uint16_t rotation = nvs_config_get_u16(NVS_CONFIG_ROTATION);
    ESP_LOGI(TAG, "Rotation: %d", rotation);
    switch (rotation) {
        case 90:
            lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
            break;
        case 180:
            lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_180);
            break;
        case 270:
            lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);
            break;
        default:
            break;
    }
}

static esp_err_t read_display_config(GlobalState * GLOBAL_STATE)
{
    if (GLOBAL_STATE->DEVICE_CONFIG.pins.i80 != NULL) {
        const DisplayConfig * display_config = get_display_config("ST7789 (320x170)");
        if (display_config) {
            GLOBAL_STATE->DISPLAY_CONFIG = *display_config;
            ESP_LOGI(TAG, "%s", GLOBAL_STATE->DISPLAY_CONFIG.name);
            return ESP_OK;
        }
    }

    char * display_config_name = nvs_config_get_string(NVS_CONFIG_DISPLAY);
    const DisplayConfig * display_config = get_display_config(display_config_name);

    if (display_config) {
        GLOBAL_STATE->DISPLAY_CONFIG = *display_config;
        ESP_LOGI(TAG, "%s", GLOBAL_STATE->DISPLAY_CONFIG.name);
        free(display_config_name);
        return ESP_OK;
    }

    free(display_config_name);
    return ESP_FAIL;
}

esp_err_t display_init(GlobalState * GLOBAL_STATE)
{
    ESP_RETURN_ON_ERROR(read_display_config(GLOBAL_STATE), TAG, "Failed to read display config");

    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_stack_caps = MALLOC_CAP_SPIRAM;

    ESP_LOGI(TAG, "Initialize LVGL");
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL init failed");

    if (lvgl_port_lock(0)) {
        lv_log_register_print_cb(my_log_cb);
        lvgl_port_unlock();
    }

    if (GLOBAL_STATE->DISPLAY_CONFIG.display == NONE) {
        if (lvgl_port_lock(0)) {
            lv_display_create(1, 1);
            lvgl_port_unlock();
        }
        GLOBAL_STATE->SYSTEM_MODULE.is_screen_active = false;
        return ESP_OK;
    }

    if (GLOBAL_STATE->DISPLAY_CONFIG.display == ST7789_I80) {
        active_driver = &display_st7789_driver;
    } else {
        active_driver = &display_oled_driver;
    }

    esp_lcd_panel_io_handle_t io_handle = NULL;
    lvgl_port_display_cfg_t disp_cfg;
    esp_err_t err = active_driver->init_panel(GLOBAL_STATE, &io_handle, &active_panel, &disp_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Display panel initialization failed (%s); using virtual display (NONE)", esp_err_to_name(err));
        active_driver = NULL;
        active_panel = NULL;
        GLOBAL_STATE->DISPLAY_CONFIG = *get_display_config("NONE");
        GLOBAL_STATE->SYSTEM_MODULE.is_screen_active = false;
        if (lvgl_port_lock(0)) {
            lv_display_create(1, 1);
            lvgl_port_unlock();
        }
        return ESP_OK;
    }

    lv_disp_t * disp = lvgl_port_add_disp(&disp_cfg);
    if (!disp) {
        ESP_LOGE(TAG, "lvgl_port_add_disp failed");
        GLOBAL_STATE->SYSTEM_MODULE.is_screen_active = false;
        return ESP_FAIL;
    }

    if (lvgl_port_lock(0)) {
        display_apply_nvs_rotation(disp);
        if (active_driver->apply_theme) {
            active_driver->apply_theme(disp);
        }
        lvgl_port_unlock();
    }

    ESP_RETURN_ON_ERROR(display_on(true), TAG, "Display on failed");
    GLOBAL_STATE->SYSTEM_MODULE.is_screen_active = true;

    ESP_LOGI(TAG, "Display init success!");
    return ESP_OK;
}

esp_err_t display_on(bool enable)
{
    if (active_panel != NULL && active_driver != NULL && active_driver->set_power != NULL) {
        if (enable && !display_state_on) {
            ESP_RETURN_ON_ERROR(active_driver->set_power(true, active_panel), TAG, "Driver set power on failed");
            display_state_on = true;
        } else if (!enable && display_state_on) {
            ESP_RETURN_ON_ERROR(active_driver->set_power(false, active_panel), TAG, "Driver set power off failed");
            display_state_on = false;
        }
    }
    return ESP_OK;
}

const DisplayConfig * get_display_config(const char * name)
{
    if (!name) return NULL;
    for (int i = 0 ; i < ARRAY_SIZE(display_configs); i++) {
        if (strcmp(display_configs[i].name, name) == 0) {
            return &display_configs[i];
        }
    }
    return NULL;
}
