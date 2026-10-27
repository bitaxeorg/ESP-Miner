#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "device_config.h"
#include "display.h"

#define TEST_STUB_GLOBAL_STATE_H
#define GLOBAL_STATE_H_
typedef struct GlobalState {
    DeviceConfig DEVICE_CONFIG;
    DisplayConfig DISPLAY_CONFIG;
    struct { bool is_screen_active; } SYSTEM_MODULE;
} GlobalState;

#define nvs_config_get_string display_test_get_string
#define nvs_config_get_u16 display_test_get_u16
#define nvs_config_has_key display_test_has_key
#define nvs_config_get_bool display_test_get_bool
#define nvs_config_get_i32 display_test_get_i32
#define lvgl_port_init display_test_lvgl_init
#define lvgl_port_lock display_test_lvgl_lock
#define lvgl_port_unlock display_test_lvgl_unlock
#define lvgl_port_add_disp display_test_add_disp
#define lv_log_register_print_cb display_test_log_callback
#define lv_display_create display_test_create
#define lv_display_set_rotation display_test_rotation
#include "sdkconfig.h"
#undef CONFIG_ENABLE_BAP
#define CONFIG_ENABLE_BAP 1
#undef CONFIG_GPIO_I2C_SDA
#define CONFIG_GPIO_I2C_SDA 12
#undef CONFIG_GPIO_I2C_SCL
#define CONFIG_GPIO_I2C_SCL 13
#define TAG display_test_config_tag
#include "../../../main/device_config.c"
#undef TAG
#include "../../../main/display.c"

static const char *saved_display;
static unsigned display_reads, oled_inits, color_inits, virtual_displays, lvgl_inits;
static lv_display_t test_display;

char *display_test_get_string(NvsConfigKey key)
{
    if (key == NVS_CONFIG_BOARD_VERSION) return strdup("custom-board");
    if (key == NVS_CONFIG_DEVICE_MODEL) return strdup("Gamma");
    if (key == NVS_CONFIG_ASIC_MODEL) return strdup("BM1370");
    TEST_ASSERT_EQUAL(NVS_CONFIG_DISPLAY, key);
    ++display_reads;
    return strdup(saved_display);
}
uint16_t display_test_get_u16(NvsConfigKey key)
{ TEST_ASSERT_EQUAL(NVS_CONFIG_ROTATION, key); return 0; }
bool display_test_has_key(NvsConfigKey key)
{ (void)key; return false; }
bool display_test_get_bool(NvsConfigKey key)
{ (void)key; return false; }
int32_t display_test_get_i32(NvsConfigKey key)
{ (void)key; return 0; }
esp_err_t display_test_lvgl_init(const lvgl_port_cfg_t *config)
{ (void)config; ++lvgl_inits; return ESP_OK; }
bool display_test_lvgl_lock(uint32_t timeout_ms)
{ (void)timeout_ms; return true; }
void display_test_lvgl_unlock(void) {}
lv_display_t *display_test_add_disp(const lvgl_port_display_cfg_t *config)
{ (void)config; return &test_display; }
void display_test_log_callback(void (*callback)(lv_log_level_t, const char *))
{ (void)callback; }
lv_display_t *display_test_create(int32_t width, int32_t height)
{
    TEST_ASSERT_EQUAL_INT32(1, width);
    TEST_ASSERT_EQUAL_INT32(1, height);
    ++virtual_displays;
    return &test_display;
}
void display_test_rotation(lv_display_t *display, lv_display_rotation_t rotation)
{ (void)display; (void)rotation; }

static esp_err_t init_panel(esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel,
                            lvgl_port_display_cfg_t *config)
{
    *io = (esp_lcd_panel_io_handle_t)1;
    *panel = (esp_lcd_panel_handle_t)1;
    *config = (lvgl_port_display_cfg_t){0};
    return ESP_OK;
}
static esp_err_t init_oled(GlobalState *state, esp_lcd_panel_io_handle_t *io,
                            esp_lcd_panel_handle_t *panel, lvgl_port_display_cfg_t *config)
{ (void)state; ++oled_inits; return init_panel(io, panel, config); }
static esp_err_t init_color(GlobalState *state, esp_lcd_panel_io_handle_t *io,
                            esp_lcd_panel_handle_t *panel, lvgl_port_display_cfg_t *config)
{ (void)state; ++color_inits; return init_panel(io, panel, config); }
const DisplayDriver display_oled_driver = { .init_panel = init_oled };
const DisplayDriver display_st7789_driver = { .init_panel = init_color };

static GlobalState fixture(DeviceConfig config, const char *saved)
{
    display_reads = oled_inits = color_inits = virtual_displays = lvgl_inits = 0;
    active_driver = NULL;
    active_panel = NULL;
    display_state_on = false;
    saved_display = saved;
    return (GlobalState){ .DEVICE_CONFIG = config, .SYSTEM_MODULE.is_screen_active = true };
}
static DeviceConfig profile(const char *board)
{
    for (unsigned i = 0; i < ARRAY_SIZE(default_configs); ++i) {
        if (strcmp(board, default_configs[i].board_version) == 0) return default_configs[i];
    }
    TEST_FAIL_MESSAGE("Missing board profile");
    return (DeviceConfig){0};
}
static void assert_virtual_display(GlobalState *state)
{
    TEST_ASSERT_EQUAL(ESP_OK, display_init(state));
    TEST_ASSERT_EQUAL_UINT32(1, lvgl_inits);
    TEST_ASSERT_EQUAL(NONE, state->DISPLAY_CONFIG.display);
    TEST_ASSERT_EQUAL_UINT32(1, virtual_displays);
    TEST_ASSERT_EQUAL_UINT32(0, oled_inits);
    TEST_ASSERT_EQUAL_UINT32(0, color_inits);
    TEST_ASSERT_FALSE(state->SYSTEM_MODULE.is_screen_active);
}

TEST_CASE("Bonanza ignores an old saved OLED setting", "[asic][display]")
{
    const char *saved[] = { DEFAULT_DISPLAY, "SSD1309 (128x64)" };
    for (unsigned i = 0; i < ARRAY_SIZE(saved); ++i) {
        GlobalState state = fixture(profile("1004"), saved[i]);
        TEST_ASSERT_NULL(state.DEVICE_CONFIG.pins.oled);
        TEST_ASSERT_NULL(state.DEVICE_CONFIG.pins.i80);
        TEST_ASSERT_EQUAL(ESP_OK, device_pins_init(&state.DEVICE_CONFIG.pins));
        TEST_ASSERT_NOT_NULL(state.DEVICE_CONFIG.pins.i2c);
        TEST_ASSERT_NULL(state.DEVICE_CONFIG.pins.oled);
        assert_virtual_display(&state);
        TEST_ASSERT_EQUAL_UINT32(0, display_reads);
    }
}

TEST_CASE("Absent display pins select NONE independently of board family", "[asic][display]")
{
    GlobalState state = fixture(profile("603"), DEFAULT_DISPLAY);
    state.DEVICE_CONFIG.pins.oled = NULL;
    assert_virtual_display(&state);
    TEST_ASSERT_EQUAL_UINT32(0, display_reads);
}

TEST_CASE("Existing OLED boards retain saved panel selection", "[asic][display]")
{
    for (unsigned i = 0; i < ARRAY_SIZE(default_configs); ++i) {
        DeviceConfig config = default_configs[i];
        if (config.family.id == BONANZA || config.pins.i80 != NULL) continue;
        TEST_ASSERT_NOT_NULL(config.pins.oled);
        GlobalState state = fixture(config, "SH1107 (64x128)");
        TEST_ASSERT_EQUAL(ESP_OK, display_init(&state));
        TEST_ASSERT_EQUAL(SH1107, state.DISPLAY_CONFIG.display);
        TEST_ASSERT_EQUAL_UINT32(1, display_reads);
        TEST_ASSERT_EQUAL_UINT32(1, oled_inits);
        TEST_ASSERT_EQUAL_UINT32(0, color_inits);
        TEST_ASSERT_EQUAL_UINT32(0, virtual_displays);
        TEST_ASSERT_TRUE(state.SYSTEM_MODULE.is_screen_active);
    }
}

TEST_CASE("Color boards select I80 panels over a saved OLED setting", "[asic][display]")
{
    for (unsigned i = 0; i < ARRAY_SIZE(default_configs); ++i) {
        DeviceConfig config = default_configs[i];
        if (config.pins.i80 == NULL) continue;
        TEST_ASSERT_NULL(config.pins.oled);
        GlobalState state = fixture(config, DEFAULT_DISPLAY);
        TEST_ASSERT_EQUAL(ESP_OK, display_init(&state));
        TEST_ASSERT_EQUAL(ST7789_I80, state.DISPLAY_CONFIG.display);
        TEST_ASSERT_EQUAL_UINT32(0, display_reads);
        TEST_ASSERT_EQUAL_UINT32(0, oled_inits);
        TEST_ASSERT_EQUAL_UINT32(1, color_inits);
        TEST_ASSERT_EQUAL_UINT32(0, virtual_displays);
        TEST_ASSERT_TRUE(state.SYSTEM_MODULE.is_screen_active);
    }
}

TEST_CASE("Saved NONE selection still creates a virtual display", "[asic][display]")
{
    GlobalState state = fixture(profile("603"), "NONE");
    TEST_ASSERT_NOT_NULL(state.DEVICE_CONFIG.pins.oled);
    assert_virtual_display(&state);
    TEST_ASSERT_EQUAL_UINT32(1, display_reads);
}

TEST_CASE("OLED pins follow shared I2C overrides without enabling a color-board OLED", "[asic][display]")
{
    const char *boards[] = { "603", "1201", "1300" };
    for (unsigned i = 0; i < ARRAY_SIZE(boards); ++i) {
        DeviceConfig config = profile(boards[i]);
        TEST_ASSERT_EQUAL(ESP_OK, device_pins_init(&config.pins));
        TEST_ASSERT_EQUAL_INT(12, config.pins.i2c->sda);
        TEST_ASSERT_EQUAL_INT(13, config.pins.i2c->scl);
        if (config.pins.i80 != NULL) TEST_ASSERT_NULL(config.pins.oled);
        else TEST_ASSERT_EQUAL_PTR(config.pins.i2c, config.pins.oled);
    }
}

TEST_CASE("Custom boards retain their default OLED on the configured I2C bus", "[asic][display]")
{
    GlobalState state = fixture((DeviceConfig){0}, DEFAULT_DISPLAY);
    TEST_ASSERT_EQUAL(ESP_OK, device_config_init(&state));
    TEST_ASSERT_NOT_NULL(state.DEVICE_CONFIG.pins.i2c);
    TEST_ASSERT_EQUAL_PTR(state.DEVICE_CONFIG.pins.i2c, state.DEVICE_CONFIG.pins.oled);
    TEST_ASSERT_EQUAL(ESP_OK, display_init(&state));
    TEST_ASSERT_EQUAL(SSD1306, state.DISPLAY_CONFIG.display);
    TEST_ASSERT_EQUAL_UINT32(1, oled_inits);
    free((void *)state.DEVICE_CONFIG.board_version);
}
