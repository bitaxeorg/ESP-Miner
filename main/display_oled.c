#include <string.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "global_state.h"
#include "nvs_config.h"
#include "i2c_bitaxe.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_lcd_sh1107.h"
#include "lvgl.h"
#include "lvgl__lvgl/src/themes/lv_theme_private.h"
#include "display.h"
#include "display_oled.h"

#define DISPLAY_I2C_ADDRESS                  0x3C
#define LCD_CMD_BITS                         8
#define LCD_PARAM_BITS                       8

static const char * TAG = "display_oled";

static lv_theme_t oled_theme;
static lv_style_t oled_scr_style;
extern const lv_font_t lv_font_portfolio_6x8;

static void oled_theme_apply(lv_theme_t *theme, lv_obj_t *obj)
{
    if (lv_obj_get_parent(obj) == NULL) {
        lv_obj_add_style(obj, &oled_scr_style, LV_PART_MAIN);
    }
}

static void oled_apply_theme(lv_disp_t * disp)
{
    lv_style_init(&oled_scr_style);
    lv_style_set_text_font(&oled_scr_style, &lv_font_portfolio_6x8);
    lv_style_set_bg_opa(&oled_scr_style, LV_OPA_COVER);

    lv_theme_set_apply_cb(&oled_theme, oled_theme_apply);
    lv_display_set_theme(disp, &oled_theme);
}

static esp_err_t oled_init_panel(GlobalState * GLOBAL_STATE,
                                 esp_lcd_panel_io_handle_t * out_io,
                                 esp_lcd_panel_handle_t * out_panel,
                                 lvgl_port_display_cfg_t * out_disp_cfg)
{
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
    esp_lcd_panel_handle_t panel_handle = NULL;

    switch (GLOBAL_STATE->DISPLAY_CONFIG.display) {
        case SSD1306:
        case SSD1309:
            esp_lcd_panel_ssd1306_config_t ssd1306_config = {
                .height = GLOBAL_STATE->DISPLAY_CONFIG.v_res,
            };
            panel_config.vendor_config = &ssd1306_config;
            ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle), TAG, "No display found");
            break;
        case SH1107: {
            esp_lcd_panel_sh1107_config_t sh1107_config = {0};
            if (nvs_config_has_key(NVS_CONFIG_DISPLAY_OFFSET)) {
                sh1107_config.offset = (uint8_t)nvs_config_get_u16(NVS_CONFIG_DISPLAY_OFFSET);
                panel_config.vendor_config = &sh1107_config;
                ESP_LOGI(TAG, "SH1107 Display Offset override: 0x%02x", sh1107_config.offset);
            }
            ESP_RETURN_ON_ERROR(esp_lcd_new_panel_sh1107(io_handle, &panel_config, &panel_handle), TAG, "No display found");
            break;
        }
        default:
            return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_handle), TAG, "Panel reset failed");
    esp_err_t esp_lcd_panel_init_err = esp_lcd_panel_init(panel_handle);
    if (esp_lcd_panel_init_err != ESP_OK) {
        ESP_LOGE(TAG, "Panel init failed, no display connected?");
        return esp_lcd_panel_init_err;
    }

    bool invert_screen = nvs_config_get_bool(NVS_CONFIG_INVERT_SCREEN);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel_handle, invert_screen), TAG, "Panel invert failed");

    *out_io = io_handle;
    *out_panel = panel_handle;

    memset(out_disp_cfg, 0, sizeof(lvgl_port_display_cfg_t));
    out_disp_cfg->io_handle = io_handle;
    out_disp_cfg->panel_handle = panel_handle;
    out_disp_cfg->buffer_size = GLOBAL_STATE->DISPLAY_CONFIG.h_res * GLOBAL_STATE->DISPLAY_CONFIG.v_res;
    out_disp_cfg->double_buffer = true;
    out_disp_cfg->hres = GLOBAL_STATE->DISPLAY_CONFIG.h_res;
    out_disp_cfg->vres = GLOBAL_STATE->DISPLAY_CONFIG.v_res;
    out_disp_cfg->monochrome = true;
    out_disp_cfg->color_format = LV_COLOR_FORMAT_I1;
    out_disp_cfg->flags.buff_spiram = true;

    return ESP_OK;
}

bool display_oled_probe(void)
{
    i2c_master_bus_handle_t bus_handle = NULL;
    if (i2c_bitaxe_get_master_bus_handle(&bus_handle) != ESP_OK || bus_handle == NULL) {
        return false;
    }
    if (i2c_master_probe(bus_handle, 0x3C, 20) == ESP_OK) {
        return true;
    }
    if (i2c_master_probe(bus_handle, 0x3D, 20) == ESP_OK) {
        return true;
    }
    return false;
}

static bool oled_driver_probe(GlobalState * state)
{
    (void)state;
    return display_oled_probe();
}

static esp_err_t oled_set_power(bool enable, esp_lcd_panel_handle_t panel)
{
    return esp_lcd_panel_disp_on_off(panel, enable);
}

const DisplayDriver display_oled_driver = {
    .init_panel = oled_init_panel,
    .set_power = oled_set_power,
    .apply_theme = oled_apply_theme,
    .probe = oled_driver_probe,
};
