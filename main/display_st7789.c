#include <string.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_io_i80.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "global_state.h"
#include "lvgl.h"
#include "lvgl__lvgl/src/themes/lv_theme_private.h"
#include "display.h"
#include "display_st7789.h"

#define LCD_CMD_BITS           8
#define LCD_PARAM_BITS         8

#define LCD_I80_PIXEL_CLOCK_HZ 4000000
#define LCD_I80_BUF_LINES      40
#define LCD_I80_GAP_X          0
#define LCD_I80_GAP_Y          35

#define LCD_BK_LIGHT_ON_LEVEL  1
#define LCD_BK_LIGHT_OFF_LEVEL 0
#define LCD_PWR_ON_LEVEL       1

static const char * TAG = "display_st7789";

static const I80Pins * current_st7789_pins = NULL;
static lv_theme_t st7789_theme;
static lv_style_t st7789_scr_style;
static lv_font_t lcd_font_unscii_16;

extern const lv_font_t lv_font_portfolio_6x8;
LV_FONT_DECLARE(lv_font_unscii_16);

static void st7789_theme_apply(lv_theme_t *theme, lv_obj_t *obj)
{
    if (lv_obj_get_parent(obj) == NULL) {
        lv_obj_add_style(obj, &st7789_scr_style, LV_PART_MAIN);
    }
}

static void st7789_apply_theme(lv_disp_t * disp)
{
    lv_style_init(&st7789_scr_style);
    lcd_font_unscii_16 = lv_font_unscii_16;
    lcd_font_unscii_16.fallback = &lv_font_portfolio_6x8;
    lv_style_set_text_font(&st7789_scr_style, &lcd_font_unscii_16);
    lv_style_set_bg_opa(&st7789_scr_style, LV_OPA_COVER);
    lv_style_set_bg_color(&st7789_scr_style, lv_color_black());
    lv_style_set_text_color(&st7789_scr_style, lv_color_white());

    lv_theme_set_apply_cb(&st7789_theme, st7789_theme_apply);
    lv_display_set_theme(disp, &st7789_theme);
}

static esp_err_t st7789_init_panel(GlobalState * GLOBAL_STATE,
                                   esp_lcd_panel_io_handle_t * out_io,
                                   esp_lcd_panel_handle_t * out_panel,
                                   lvgl_port_display_cfg_t * out_disp_cfg)
{
    const I80Pins * pins = GLOBAL_STATE->DEVICE_CONFIG.pins.i80;
    ESP_RETURN_ON_FALSE(pins, ESP_ERR_INVALID_ARG, TAG, "No I80 display pin map configured");
    current_st7789_pins = pins;

    // Some boards mount the panel upside down; mirroring the other axis
    // turns the image by 180 degrees.
    const bool flip = GLOBAL_STATE->DEVICE_CONFIG.display_flip;
    const bool mirror_x = flip;
    const bool mirror_y = !flip;

    ESP_LOGI(TAG, "Configure ST7789 GPIOs");
    gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << pins->bk_light) | (1ULL << pins->rd) | (1ULL << pins->pwr),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&output_config), TAG, "Failed to configure LCD GPIOs");
    gpio_set_level(pins->rd, 1);
    gpio_set_level(pins->pwr, LCD_PWR_ON_LEVEL);
    gpio_set_level(pins->bk_light, LCD_BK_LIGHT_OFF_LEVEL);

    ESP_LOGI(TAG, "Initialize ST7789 Intel 8080 bus");
    esp_lcd_i80_bus_handle_t i80_bus = NULL;
    esp_lcd_i80_bus_config_t bus_config = {
        .dc_gpio_num = pins->dc,
        .wr_gpio_num = pins->wr,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums = {
            pins->data[0],
            pins->data[1],
            pins->data[2],
            pins->data[3],
            pins->data[4],
            pins->data[5],
            pins->data[6],
            pins->data[7],
        },
        .bus_width = 8,
        .max_transfer_bytes = GLOBAL_STATE->DISPLAY_CONFIG.h_res * LCD_I80_BUF_LINES * sizeof(uint16_t),
        .dma_burst_size = 64,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_i80_bus(&bus_config, &i80_bus), TAG, "Failed to initialize i80 bus");

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = pins->cs,
        .pclk_hz = LCD_I80_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 20,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &io_handle), TAG, "Failed to initialize i80 panel IO");

    ESP_LOGI(TAG, "Install ST7789 panel driver");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = pins->rst,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle), TAG, "No ST7789 display found");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_handle), TAG, "Panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel_handle), TAG, "Panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel_handle, true), TAG, "Panel invert failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(panel_handle, true), TAG, "Panel swap XY failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(panel_handle, mirror_x, mirror_y), TAG, "Panel mirror failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(panel_handle, LCD_I80_GAP_X, LCD_I80_GAP_Y), TAG, "Panel gap failed");

    *out_io = io_handle;
    *out_panel = panel_handle;

    memset(out_disp_cfg, 0, sizeof(lvgl_port_display_cfg_t));
    out_disp_cfg->io_handle = io_handle;
    out_disp_cfg->panel_handle = panel_handle;
    out_disp_cfg->buffer_size = GLOBAL_STATE->DISPLAY_CONFIG.h_res * LCD_I80_BUF_LINES;
    out_disp_cfg->double_buffer = true;
    out_disp_cfg->hres = GLOBAL_STATE->DISPLAY_CONFIG.h_res;
    out_disp_cfg->vres = GLOBAL_STATE->DISPLAY_CONFIG.v_res;
    out_disp_cfg->monochrome = false;
    out_disp_cfg->rotation.swap_xy = true;
    out_disp_cfg->rotation.mirror_x = mirror_x;
    out_disp_cfg->rotation.mirror_y = mirror_y;
    out_disp_cfg->color_format = LV_COLOR_FORMAT_RGB565;
    out_disp_cfg->flags.buff_dma = true;
    out_disp_cfg->flags.buff_spiram = true;

    return ESP_OK;
}

static esp_err_t st7789_set_power(bool enable, esp_lcd_panel_handle_t panel)
{
    if (enable) {
        if (current_st7789_pins) {
            gpio_set_level(current_st7789_pins->pwr, LCD_PWR_ON_LEVEL);
        }
        ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "Panel display on failed");
        if (current_st7789_pins) {
            gpio_set_level(current_st7789_pins->bk_light, LCD_BK_LIGHT_ON_LEVEL);
        }
    } else {
        if (current_st7789_pins) {
            gpio_set_level(current_st7789_pins->bk_light, LCD_BK_LIGHT_OFF_LEVEL);
        }
        ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, false), TAG, "Panel display off failed");
    }
    return ESP_OK;
}

bool display_st7789_probe(GlobalState * GLOBAL_STATE)
{
    if (GLOBAL_STATE == NULL || GLOBAL_STATE->DEVICE_CONFIG.pins.i80 == NULL) {
        return false;
    }
    return true;
}

const DisplayDriver display_st7789_driver = {
    .init_panel = st7789_init_panel,
    .set_power = st7789_set_power,
    .apply_theme = st7789_apply_theme,
    .probe = display_st7789_probe,
};
