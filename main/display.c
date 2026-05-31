#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_io_i80.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
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

#define LCD_I80_PIXEL_CLOCK_HZ  4000000
#define LCD_I80_BUF_LINES       40
#define LCD_I80_H_RES           320
#define LCD_I80_V_RES           170
#define LCD_I80_GAP_X           0
#define LCD_I80_GAP_Y           35

#define LCD_PIN_DATA0           GPIO_NUM_39
#define LCD_PIN_DATA1           GPIO_NUM_40
#define LCD_PIN_DATA2           GPIO_NUM_41
#define LCD_PIN_DATA3           GPIO_NUM_42
#define LCD_PIN_DATA4           GPIO_NUM_45
#define LCD_PIN_DATA5           GPIO_NUM_46
#define LCD_PIN_DATA6           GPIO_NUM_47
#define LCD_PIN_DATA7           GPIO_NUM_48
#define LCD_PIN_RD              GPIO_NUM_9
#define LCD_PIN_PWR             GPIO_NUM_15
#define LCD_PIN_WR              GPIO_NUM_8
#define LCD_PIN_CS              GPIO_NUM_6
#define LCD_PIN_DC              GPIO_NUM_7
#define LCD_PIN_RST             GPIO_NUM_5
#define LCD_PIN_BK_LIGHT        GPIO_NUM_38
#define LCD_BK_LIGHT_ON_LEVEL   1
#define LCD_BK_LIGHT_OFF_LEVEL  0
#define LCD_PWR_ON_LEVEL        1
#define LCD_PWR_OFF_LEVEL       0
#define LCD_BOOT_PROBE_MS       750

static const char * TAG = "display";
static const char * LVGL_TAG = "lvgl";

static esp_lcd_panel_handle_t panel_handle = NULL;
static bool display_state_on = false;
static bool display_has_backlight = false;

static lv_theme_t theme;
static lv_style_t scr_style;
static lv_font_t lcd_font_unscii_16;

extern const lv_font_t lv_font_portfolio_6x8;
LV_FONT_DECLARE(lv_font_unscii_16);

esp_err_t display_on(bool display_on);
static void my_log_cb(lv_log_level_t level, const char * buf);
static esp_err_t st7789_boot_probe(void);

static void theme_apply(lv_theme_t *theme, lv_obj_t *obj) {
    if (lv_obj_get_parent(obj) == NULL) {
        lv_obj_add_style(obj, &scr_style, LV_PART_MAIN);
    }
}

static esp_err_t read_display_config(GlobalState * GLOBAL_STATE)
{
    if (GLOBAL_STATE->DEVICE_CONFIG.family.id == GAMMA_HEX) {
        const DisplayConfig * display_config = get_display_config("ST7789 (320x170)");
        GLOBAL_STATE->DISPLAY_CONFIG = *display_config;
        ESP_LOGI(TAG, "%s", GLOBAL_STATE->DISPLAY_CONFIG.name);
        return ESP_OK;
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

static esp_err_t init_i80_st7789_display(GlobalState * GLOBAL_STATE, const lvgl_port_cfg_t *lvgl_cfg)
{
    ESP_LOGI(TAG, "Configure ST7789 GPIOs");
    gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << LCD_PIN_BK_LIGHT) | (1ULL << LCD_PIN_RD) | (1ULL << LCD_PIN_PWR),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&output_config), TAG, "Failed to configure LCD GPIOs");
    gpio_set_level(LCD_PIN_RD, 1);
    gpio_set_level(LCD_PIN_PWR, LCD_PWR_ON_LEVEL);
    gpio_set_level(LCD_PIN_BK_LIGHT, LCD_BK_LIGHT_OFF_LEVEL);

    ESP_LOGI(TAG, "Initialize ST7789 Intel 8080 bus");
    esp_lcd_i80_bus_handle_t i80_bus = NULL;
    esp_lcd_i80_bus_config_t bus_config = {
        .dc_gpio_num = LCD_PIN_DC,
        .wr_gpio_num = LCD_PIN_WR,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums = {
            LCD_PIN_DATA0,
            LCD_PIN_DATA1,
            LCD_PIN_DATA2,
            LCD_PIN_DATA3,
            LCD_PIN_DATA4,
            LCD_PIN_DATA5,
            LCD_PIN_DATA6,
            LCD_PIN_DATA7,
        },
        .bus_width = 8,
        .max_transfer_bytes = LCD_I80_H_RES * LCD_I80_BUF_LINES * sizeof(uint16_t),
        .psram_trans_align = 64,
        .sram_trans_align = 4,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_i80_bus(&bus_config, &i80_bus), TAG, "Failed to initialize i80 bus");

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = LCD_PIN_CS,
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
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle), TAG, "No ST7789 display found");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_handle), TAG, "Panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel_handle), TAG, "Panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel_handle, true), TAG, "Panel invert failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(panel_handle, true), TAG, "Panel swap XY failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(panel_handle, true, false), TAG, "Panel mirror failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(panel_handle, LCD_I80_GAP_X, LCD_I80_GAP_Y), TAG, "Panel gap failed");
    display_has_backlight = true;

#if LCD_BOOT_PROBE_MS > 0
    ESP_RETURN_ON_ERROR(st7789_boot_probe(), TAG, "ST7789 boot probe failed");
#endif

    ESP_LOGI(TAG, "Initialize LVGL");
    ESP_RETURN_ON_ERROR(lvgl_port_init(lvgl_cfg), TAG, "LVGL init failed");
    lv_log_register_print_cb(my_log_cb);

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = LCD_I80_H_RES * LCD_I80_BUF_LINES,
        .double_buffer = true,
        .hres = GLOBAL_STATE->DISPLAY_CONFIG.h_res,
        .vres = GLOBAL_STATE->DISPLAY_CONFIG.v_res,
        .monochrome = false,
        .rotation = {
            .swap_xy = true,
            .mirror_x = true,
            .mirror_y = false,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = true,
            .swap_bytes = false,
            .sw_rotate = false,
        }
    };

    lv_disp_t * disp = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(disp, ESP_FAIL, TAG, "lvgl_port_add_disp failed");

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
        lcd_font_unscii_16 = lv_font_unscii_16;
        lcd_font_unscii_16.fallback = &lv_font_portfolio_6x8;
        lv_style_set_text_font(&scr_style, &lcd_font_unscii_16);
        lv_style_set_bg_opa(&scr_style, LV_OPA_COVER);
        lv_style_set_bg_color(&scr_style, lv_color_black());
        lv_style_set_text_color(&scr_style, lv_color_white());

        lv_theme_set_apply_cb(&theme, theme_apply);
        lv_display_set_theme(disp, &theme);
        lvgl_port_unlock();
    }

    ESP_RETURN_ON_ERROR(display_on(true), TAG, "Display on failed");
    GLOBAL_STATE->SYSTEM_MODULE.is_screen_active = true;

    ESP_LOGI(TAG, "ST7789 display init success!");
    return ESP_OK;
}

static esp_err_t st7789_fill_color(uint16_t color)
{
    const size_t pixels = LCD_I80_H_RES * LCD_I80_BUF_LINES;
    uint16_t *buf = heap_caps_malloc(pixels * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "Failed to allocate ST7789 boot probe buffer");

    for (size_t i = 0; i < pixels; i++) {
        buf[i] = color;
    }

    esp_err_t ret = ESP_OK;
    for (int y = 0; y < LCD_I80_V_RES; y += LCD_I80_BUF_LINES) {
        int y_end = y + LCD_I80_BUF_LINES;
        if (y_end > LCD_I80_V_RES) {
            y_end = LCD_I80_V_RES;
        }
        ret = esp_lcd_panel_draw_bitmap(panel_handle, 0, y, LCD_I80_H_RES, y_end, buf);
        if (ret != ESP_OK) {
            break;
        }
    }

    free(buf);
    return ret;
}

static esp_err_t st7789_boot_probe(void)
{
    ESP_LOGI(TAG, "Draw ST7789 boot probe");
    ESP_RETURN_ON_ERROR(display_on(true), TAG, "Display on failed");
    ESP_RETURN_ON_ERROR(st7789_fill_color(0xF800), TAG, "Red fill failed");
    vTaskDelay(pdMS_TO_TICKS(LCD_BOOT_PROBE_MS / 3));
    ESP_RETURN_ON_ERROR(st7789_fill_color(0x07E0), TAG, "Green fill failed");
    vTaskDelay(pdMS_TO_TICKS(LCD_BOOT_PROBE_MS / 3));
    ESP_RETURN_ON_ERROR(st7789_fill_color(0x001F), TAG, "Blue fill failed");
    vTaskDelay(pdMS_TO_TICKS(LCD_BOOT_PROBE_MS / 3));
    return ESP_OK;
}

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

esp_err_t display_init(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    ESP_RETURN_ON_ERROR(read_display_config(GLOBAL_STATE), TAG, "Failed to read display config");

    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();

    lvgl_cfg.task_stack_caps = MALLOC_CAP_SPIRAM;

    if (GLOBAL_STATE->DISPLAY_CONFIG.display == NONE) {
        ESP_LOGI(TAG, "Initialize LVGL");
        ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL init failed");
        if (lvgl_port_lock(0)) {
            lv_display_create(1, 1);
            lvgl_port_unlock();
        }
        return ESP_OK;
    }

    if (GLOBAL_STATE->DISPLAY_CONFIG.display == ST7789_I80) {
        return init_i80_st7789_display(GLOBAL_STATE, &lvgl_cfg);
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
        case ST7789_I80:
            return ESP_FAIL;
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
        case ST7789_I80:
            return ESP_FAIL;
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

        if (GLOBAL_STATE->DISPLAY_CONFIG.display == SH1107) {
            uint8_t display_offset = nvs_config_get_u16(NVS_CONFIG_DISPLAY_OFFSET);
            if (display_offset != LCD_SH1107_PARAM_DEFAULT_DISP_OFFSET) {
                ESP_LOGI(TAG, "SH1107 Display Offset: 0x%02x", display_offset);
                esp_lcd_panel_io_tx_param(io_handle, LCD_SH1107_I2C_CMD, (uint8_t[]) { LCD_SH1107_PARAM_SET_DISP_OFFSET, display_offset }, 2);
            }
        }
    }

    ESP_LOGI(TAG, "Initialize LVGL");

    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL init failed");

    if (lvgl_port_lock(0)) {
        lv_log_register_print_cb(my_log_cb);
        lvgl_port_unlock();
    }

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
            if (display_has_backlight) {
                gpio_set_level(LCD_PIN_PWR, LCD_PWR_ON_LEVEL);
            }
            ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel_handle, true), TAG, "Panel display on failed");
            if (display_has_backlight) {
                gpio_set_level(LCD_PIN_BK_LIGHT, LCD_BK_LIGHT_ON_LEVEL);
            }
            display_state_on = true;
        }
        else if (!display_on && display_state_on)
        {
            if (display_has_backlight) {
                gpio_set_level(LCD_PIN_BK_LIGHT, LCD_BK_LIGHT_OFF_LEVEL);
            }
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
