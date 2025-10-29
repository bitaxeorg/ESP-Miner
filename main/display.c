#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "lvgl.h"
#include "lvgl__lvgl/src/themes/lv_theme_private.h"
#include "esp_lvgl_port.h"
#include "global_state.h"
#include "nvs_config.h"
#include "i2c_bitaxe.h"
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_lcd_sh1107.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define DISPLAY_I2C_ADDRESS    0x3C

#define LCD_CMD_BITS           8
#define LCD_PARAM_BITS         8

static const char * TAG = "display";

static esp_lcd_panel_handle_t panel_handle = NULL;
static bool display_state_on = false;

static lv_theme_t theme;
static lv_style_t scr_style;

extern const lv_font_t lv_font_portfolio_6x8;

esp_err_t display_on(bool display_on);

static void theme_apply(lv_theme_t *theme, lv_obj_t *obj) {
    if (lv_obj_get_parent(obj) == NULL) {
        lv_obj_add_style(obj, &scr_style, LV_PART_MAIN);
    }
}

static esp_err_t read_display_config(GlobalState * GLOBAL_STATE)
{
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

esp_err_t display_init(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    ESP_RETURN_ON_ERROR(read_display_config(GLOBAL_STATE), TAG, "Failed to read display config");

    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();

    lvgl_cfg.task_stack_caps = MALLOC_CAP_SPIRAM;

    if (GLOBAL_STATE->DISPLAY_CONFIG.display == NONE) {
        ESP_LOGI(TAG, "Initialize LVGL");
        ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL init failed");
        lv_display_create(1, 1);
        return ESP_OK;
    }

    i2c_master_bus_handle_t i2c_master_bus_handle;
    ESP_RETURN_ON_ERROR(i2c_bitaxe_get_master_bus_handle(&i2c_master_bus_handle), TAG, "Failed to get i2c master bus handle");

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_i2c_config_t io_config = {
        .scl_speed_hz = I2C_BUS_SPEED_HZ,
        .dev_addr = DISPLAY_I2C_ADDRESS,
        .control_phase_bytes = 1,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
    };

    switch (GLOBAL_STATE->DISPLAY_CONFIG.display) {
        case SSD1306:
        case SSD1309:
            io_config.dc_bit_offset = 6;
            break;
        case SH1107:
            io_config.dc_bit_offset = 0;
            io_config.flags.disable_control_phase = 1;
            break;
        default:
            return ESP_FAIL;
    }
    
    esp_lcd_panel_io_handle_t io_handle = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_master_bus_handle, &io_config, &io_handle), TAG, "Failed to initialise i2c panel bus");

    ESP_LOGI(TAG, "Install panel driver");
    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,
    };

    switch (GLOBAL_STATE->DISPLAY_CONFIG.display) {
        case SSD1306:
        case SSD1309:
            esp_lcd_panel_ssd1306_config_t ssd1306_config = {
                .height = GLOBAL_STATE->DISPLAY_CONFIG.v_res,
            };
            panel_config.vendor_config = &ssd1306_config;
            ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle), TAG, "No display found");
            break;
        case SH1107:
            ESP_RETURN_ON_ERROR(esp_lcd_new_panel_sh1107(io_handle, &panel_config, &panel_handle), TAG, "No display found");
            break;
        default:
            return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_handle), TAG, "Panel reset failed");
    esp_err_t esp_lcd_panel_init_err = esp_lcd_panel_init(panel_handle);
    if (esp_lcd_panel_init_err != ESP_OK) {
        ESP_LOGE(TAG, "Panel init failed, no display connected?");
    }  else {
        bool invert_screen = nvs_config_get_bool(NVS_CONFIG_INVERT_SCREEN);
        ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel_handle, invert_screen), TAG, "Panel invert failed");
        // ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(panel_handle, false, false), TAG, "Panel mirror failed");
    }

    ESP_LOGI(TAG, "Initialize LVGL");

    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL init failed");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = GLOBAL_STATE->DISPLAY_CONFIG.h_res * GLOBAL_STATE->DISPLAY_CONFIG.v_res,
        .double_buffer = true,
        .hres = GLOBAL_STATE->DISPLAY_CONFIG.h_res,
        .vres = GLOBAL_STATE->DISPLAY_CONFIG.v_res,
        .monochrome = true,
        .color_format = LV_COLOR_FORMAT_I1,
        .flags = {
            .swap_bytes = false,
            .sw_rotate = false,
        }
    };

    lv_disp_t * disp = lvgl_port_add_disp(&disp_cfg);
     if (!disp) { // Check if disp is NULL
        ESP_LOGE(TAG, "lvgl_port_add_disp failed!");
        // Potential cleanup
        // if (panel_handle) esp_lcd_panel_del(panel_handle);
        // if (io_handle) esp_lcd_panel_io_del(io_handle);
        return ESP_FAIL;
    }

    if (esp_lcd_panel_init_err == ESP_OK) {
        if (lvgl_port_lock(0)) {

            uint16_t rotation = nvs_config_get_u16(NVS_CONFIG_ROTATION);

            ESP_LOGI(TAG, "Rotation: %d", rotation);
            switch(rotation) {
                case 90:
                    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
                    break;
                case 180:
                    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_180);
                    break;
                case 270:
                    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);
                    break;
            }

            lv_style_init(&scr_style);
            lv_style_set_text_font(&scr_style, &lv_font_portfolio_6x8);
            lv_style_set_bg_opa(&scr_style, LV_OPA_COVER);

            lv_theme_set_apply_cb(&theme, theme_apply);
            
            lv_display_set_theme(disp, &theme);
            lvgl_port_unlock();
        }

        // Only turn on the screen when it has been cleared
        ESP_RETURN_ON_ERROR(display_on(true), TAG, "Display on failed");

        GLOBAL_STATE->SYSTEM_MODULE.is_screen_active = true;
    } else {
        ESP_LOGW(TAG, "No display found or panel init failed. Screen not active.");
        GLOBAL_STATE->SYSTEM_MODULE.is_screen_active = false;
    }

    ESP_LOGI(TAG, "Display init success!");

    return ESP_OK;
}

esp_err_t display_on(bool display_on)
{
    if (NULL != panel_handle) {
        if (display_on && !display_state_on) {
            ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel_handle, true), TAG, "Panel display on failed");
            display_state_on = true;
        }
        else if (!display_on && display_state_on)
        {
            ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel_handle, false), TAG, "Panel display off failed");
            display_state_on = false;
        }
    }

    return ESP_OK;
}

const DisplayConfig * get_display_config(const char * name)
{
    for (int i = 0 ; i < ARRAY_SIZE(display_configs); i++) {
        if (strcmp(display_configs[i].name, name) == 0) {
            return &display_configs[i];
        }
    }
    return NULL;
}
