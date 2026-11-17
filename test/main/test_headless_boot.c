#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "../../main/global_state.h"
#include "../../main/display_driver.h"
#include "../../main/nvs_config.h"

typedef enum {
    DISPLAY_WORKS,
    PANEL_INIT_FAILS,
    DISPLAY_REGISTRATION_FAILS,
    DISPLAY_POWER_ON_FAILS,
} DisplayOutcome;

static GlobalState fixture;
static const char * configured_display;
static DisplayOutcome display_outcome;
static int64_t headless_time_us;
static int lock_depth;
static void (*before_lock)(void);
static int panel, panel_io;
static lv_display_t * registered_display;
static bool panel_deleted, panel_io_deleted;

static int64_t headless_get_time(void)
{
    return headless_time_us;
}

static bool headless_get_bool(NvsConfigKey key)
{
    TEST_ASSERT_EQUAL(NVS_CONFIG_OTA_BUTTON_REQUIRED, key);
    return true;
}

static char * headless_get_string(NvsConfigKey key)
{
    TEST_ASSERT_EQUAL(NVS_CONFIG_DISPLAY, key);
    return strdup(configured_display);
}

static uint16_t headless_get_u16(NvsConfigKey key)
{
    TEST_ASSERT_EQUAL(NVS_CONFIG_ROTATION, key);
    return 0;
}

static int32_t headless_get_i32(NvsConfigKey key)
{
    TEST_ASSERT_EQUAL(NVS_CONFIG_DISPLAY_TIMEOUT, key);
    return -1;
}

static esp_err_t headless_get_rssi(int8_t * rssi)
{
    *rssi = -60;
    return ESP_OK;
}

static esp_err_t headless_lvgl_init(const lvgl_port_cfg_t * config)
{
    lv_init();
    return ESP_OK;
}

static bool headless_lvgl_lock(uint32_t timeout_ms)
{
    if (before_lock) {
        void (*callback)(void) = before_lock;
        before_lock = NULL;
        callback();
    }
    lock_depth++;
    return true;
}

static void headless_lvgl_unlock(void)
{
    TEST_ASSERT_GREATER_THAN_INT(0, lock_depth);
    lock_depth--;
}

static esp_err_t headless_panel_init(GlobalState * state, esp_lcd_panel_io_handle_t * io,
                                    esp_lcd_panel_handle_t * handle, lvgl_port_display_cfg_t * config)
{
    if (display_outcome == PANEL_INIT_FAILS) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *io = (esp_lcd_panel_io_handle_t)&panel_io;
    *handle = (esp_lcd_panel_handle_t)&panel;
    *config = (lvgl_port_display_cfg_t){
        .io_handle = *io,
        .panel_handle = *handle,
        .hres = state->DISPLAY_CONFIG.h_res,
        .vres = state->DISPLAY_CONFIG.v_res,
    };
    return ESP_OK;
}

static lv_display_t * headless_add_display(const lvgl_port_display_cfg_t * config)
{
    if (display_outcome == DISPLAY_REGISTRATION_FAILS) {
        return NULL;
    }
    registered_display = lv_display_create(config->hres, config->vres);
    TEST_ASSERT_NOT_NULL(registered_display);
    return registered_display;
}

static esp_err_t headless_panel_power(bool enable, esp_lcd_panel_handle_t handle)
{
    TEST_ASSERT_EQUAL_PTR(&panel, handle);
    return enable && display_outcome == DISPLAY_POWER_ON_FAILS ? ESP_ERR_INVALID_RESPONSE : ESP_OK;
}

static esp_err_t headless_remove_display(lv_display_t * display)
{
    TEST_ASSERT_EQUAL_PTR(registered_display, display);
    TEST_ASSERT_GREATER_THAN_INT(0, lock_depth);
    lv_display_delete(display);
    registered_display = NULL;
    return ESP_OK;
}

static esp_err_t headless_delete_panel(esp_lcd_panel_handle_t handle)
{
    TEST_ASSERT_EQUAL_PTR(&panel, handle);
    TEST_ASSERT_NULL(registered_display);
    panel_deleted = true;
    return ESP_OK;
}

static esp_err_t headless_delete_panel_io(esp_lcd_panel_io_handle_t handle)
{
    TEST_ASSERT_EQUAL_PTR(&panel_io, handle);
    TEST_ASSERT_TRUE(panel_deleted);
    panel_io_deleted = true;
    return ESP_OK;
}

static esp_err_t headless_gpio_config(const gpio_config_t * config)
{
    return ESP_OK;
}

static esp_err_t headless_add_gpio_handler(gpio_num_t gpio, gpio_isr_t handler, void * arg)
{
    return ESP_OK;
}

const DisplayDriver display_oled_driver = {
    .init_panel = headless_panel_init,
    .set_power = headless_panel_power,
};
const DisplayDriver display_st7789_driver = {0};

// The startup test creates real screen objects without rendering their artwork.
const lv_img_dsc_t bitaxe_logo = {0};
const lv_img_dsc_t bitaxe_logo_large = {0};
const lv_img_dsc_t osmu_logo = {0};
const lv_img_dsc_t identify_text = {0};
const lv_img_dsc_t bitaxe_background = {0};
const lv_font_t ui_font_DigitalNumbers16 = {0};
const lv_font_t ui_font_DigitalNumbers28 = {0};
const lv_font_t ui_font_OpenSansBold13 = {0};
const lv_font_t ui_font_OpenSansBold14 = {0};
const lv_font_t ui_font_AngelWish40 = {0};

#define ota_security_unlock_from_button headless_unlock_from_button
#define ota_security_lock headless_lock
#define ota_security_remaining_seconds headless_remaining_seconds
#define ota_security_is_update_allowed headless_is_update_allowed
#define esp_timer_get_time headless_get_time
#define nvs_config_get_bool headless_get_bool
#include "../../main/ota_security.c"
#undef esp_timer_get_time
#undef nvs_config_get_bool

#define lvgl_port_lock headless_lvgl_lock
#define lvgl_port_unlock headless_lvgl_unlock
#define lvgl_port_init headless_lvgl_init
#define lvgl_port_add_disp headless_add_display
#define lvgl_port_remove_disp headless_remove_display
#define esp_lcd_panel_del headless_delete_panel
#define esp_lcd_panel_io_del headless_delete_panel_io
#define nvs_config_get_string headless_get_string
#define nvs_config_get_u16 headless_get_u16
#define TAG headless_display_tag
#include "../../main/display.c"
#undef TAG
#define nvs_config_get_i32 headless_get_i32
#define get_wifi_current_rssi headless_get_rssi
#define TAG headless_screen_tag
#include "../../main/screen.c"
#undef TAG
#define gpio_config headless_gpio_config
#define gpio_isr_handler_add headless_add_gpio_handler
#define TAG headless_input_tag
#define CONFIG_GPIO_BUTTON_BOOT 0
#include "../../main/input.c"
#undef TAG
#undef gpio_isr_handler_add
#undef gpio_config

static void start_fixture(const char * display, DisplayOutcome outcome)
{
    memset(&fixture, 0, sizeof(fixture));
    fixture.SYSTEM_MODULE.ssid = "test";
    strcpy(fixture.SYSTEM_MODULE.ap_ssid, "bitaxe-test");
    fixture.DEVICE_CONFIG.family.name = "Gamma";
    fixture.DEVICE_CONFIG.board_version = "601";
    configured_display = display;
    display_outcome = outcome;
    headless_time_us = 0;
    headless_lock();
    TEST_ASSERT_EQUAL(ESP_OK, display_init(&fixture));
    TEST_ASSERT_NOT_NULL(lv_display_get_default());
    TEST_ASSERT_EQUAL(ESP_OK, input_init(screen_button_press, NULL));
    TEST_ASSERT_NOT_NULL(lv_indev_get_next(NULL));
    TEST_ASSERT_FALSE(headless_is_update_allowed());
}

static void end_fixture(void)
{
    TEST_ASSERT_EQUAL_INT(0, lock_depth);
    lv_deinit();
    GLOBAL_STATE = NULL;
    memset(screens, 0, sizeof(screens));
    registered_display = NULL;
    panel_deleted = false;
    panel_io_deleted = false;
    active_panel = NULL;
    active_driver = NULL;
    display_state_on = false;
}

static void press_boot(void)
{
    lv_indev_t * input = lv_indev_get_next(NULL);
    button_state = LV_INDEV_STATE_PRESSED;
    lv_indev_read(input);
    lv_tick_inc(50);
    button_state = LV_INDEV_STATE_RELEASED;
    lv_indev_read(input);
}

static void check_headless_boot(const char * display, DisplayOutcome outcome)
{
    start_fixture(display, outcome);
    TEST_ASSERT_EQUAL(NONE, fixture.DISPLAY_CONFIG.display);
    TEST_ASSERT_FALSE(fixture.SYSTEM_MODULE.is_screen_active);
    lv_display_t * virtual_display = lv_display_get_default();
    TEST_ASSERT_EQUAL_INT(1, lv_display_get_horizontal_resolution(virtual_display));
    TEST_ASSERT_EQUAL_INT(1, lv_display_get_vertical_resolution(virtual_display));
    TEST_ASSERT_NULL(lv_display_get_next(virtual_display));
    if (outcome == DISPLAY_REGISTRATION_FAILS || outcome == DISPLAY_POWER_ON_FAILS) {
        TEST_ASSERT_TRUE(panel_deleted);
        TEST_ASSERT_TRUE(panel_io_deleted);
    }

    TEST_ASSERT_EQUAL(ESP_OK, screen_start(&fixture));
    press_boot();
    TEST_ASSERT_TRUE(headless_is_update_allowed());
    TEST_ASSERT_EQUAL_UINT32(120, headless_remaining_seconds());
    headless_time_us += 5000000;
    TEST_ASSERT_EQUAL_UINT32(115, headless_remaining_seconds());
    press_boot();
    TEST_ASSERT_EQUAL_UINT32(120, headless_remaining_seconds());
    end_fixture();
}

TEST_CASE("BOOT unlock remains responsive with display NONE", "[ota_security]")
{
    check_headless_boot("NONE", DISPLAY_WORKS);
}

TEST_CASE("BOOT unlock remains responsive after panel initialization fails", "[ota_security]")
{
    check_headless_boot(DEFAULT_DISPLAY, PANEL_INIT_FAILS);
}

TEST_CASE("BOOT unlock remains responsive after LVGL display registration fails", "[ota_security]")
{
    check_headless_boot(DEFAULT_DISPLAY, DISPLAY_REGISTRATION_FAILS);
}

TEST_CASE("BOOT unlock remains responsive after display power-on fails", "[ota_security]")
{
    check_headless_boot(DEFAULT_DISPLAY, DISPLAY_POWER_ON_FAILS);
}

static void press_boot_while_screen_start_waits(void)
{
    TEST_ASSERT_EQUAL_INT(0, lock_depth);
    TEST_ASSERT_NULL(screens[SCR_URLS]);
    TEST_ASSERT_NULL(GLOBAL_STATE);
    press_boot();
    TEST_ASSERT_TRUE(headless_is_update_allowed());
}

TEST_CASE("BOOT unlock works before screen startup acquires the LVGL lock", "[ota_security]")
{
    start_fixture(DEFAULT_DISPLAY, DISPLAY_WORKS);
    TEST_ASSERT_TRUE(fixture.SYSTEM_MODULE.is_screen_active);
    before_lock = press_boot_while_screen_start_waits;
    TEST_ASSERT_EQUAL(ESP_OK, screen_start(&fixture));
    TEST_ASSERT_NOT_NULL(screens[SCR_URLS]);
    TEST_ASSERT_TRUE(headless_is_update_allowed());

    headless_time_us += 5000000;
    lv_screen_load(screens[SCR_URLS]);
    press_boot();
    TEST_ASSERT_EQUAL_UINT32(120, headless_remaining_seconds());
    TEST_ASSERT_EQUAL_INT(delays_ms[SCR_STATS], current_screen_delay_ms);
    end_fixture();
}
