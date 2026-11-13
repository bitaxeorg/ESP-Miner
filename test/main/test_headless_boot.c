#include "unity.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "../../main/nvs_config.h"

static int64_t headless_time_us;
static int short_click_count;

static int64_t headless_get_time(void)
{
    return headless_time_us;
}

static bool headless_get_bool(NvsConfigKey key)
{
    TEST_ASSERT_EQUAL(NVS_CONFIG_OTA_BUTTON_REQUIRED, key);
    return true;
}

static bool headless_lvgl_lock(uint32_t timeout_ms)
{
    return true;
}

static void headless_lvgl_unlock(void)
{
}

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
#define TAG headless_screen_tag
#include "../../main/screen.c"
#undef TAG
#define TAG headless_input_tag
#define CONFIG_GPIO_BUTTON_BOOT 0
#include "../../main/input.c"
#undef TAG
#undef lvgl_port_lock
#undef lvgl_port_unlock

static void headless_short_click(void)
{
    screen_button_press();
    short_click_count++;
}

static void check_headless_boot(Display configured_display)
{
    static bool lvgl_initialized;
    if (!lvgl_initialized) {
        lv_init();
        lvgl_initialized = true;
    }
    static GlobalState state;
    memset(&state, 0, sizeof(state));
    state.DISPLAY_CONFIG.display = configured_display;
    state.SYSTEM_MODULE.is_screen_active = false;
    GLOBAL_STATE = &state;
    memset(screens, 0, sizeof(screens));
    headless_time_us = 0;
    headless_lock();
    short_click_count = 0;

    lv_display_t *display = lv_display_create(1, 1);
    lv_indev_t *input = lv_indev_create();
    lv_indev_set_type(input, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(input, button_read);
    button_short_clicked_fn = headless_short_click;
    lv_indev_add_event_cb(input, button_short_clicked_event_cb, LV_EVENT_SHORT_CLICKED, NULL);

    TEST_ASSERT_FALSE(headless_is_update_allowed());
    for (int click = 1; click <= 2; click++) {
        button_state = LV_INDEV_STATE_PRESSED;
        lv_indev_read(input);
        lv_tick_inc(50);
        button_state = LV_INDEV_STATE_RELEASED;
        lv_indev_read(input);
        TEST_ASSERT_EQUAL_INT(click, short_click_count);
        TEST_ASSERT_TRUE(headless_is_update_allowed());
        TEST_ASSERT_EQUAL_UINT32(120, headless_remaining_seconds());
        headless_time_us += 1000000;
    }
    lv_indev_delete(input);
    lv_display_delete(display);
    GLOBAL_STATE = NULL;
}

TEST_CASE("BOOT unlock remains responsive with display NONE", "[ota_security]")
{
    check_headless_boot(NONE);
}

TEST_CASE("BOOT unlock remains responsive after display initialization fails", "[ota_security]")
{
    check_headless_boot(SSD1306);
}
