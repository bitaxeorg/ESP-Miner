#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "global_state.h"
#include "screen.h"

// static const char * TAG = "screen";

extern const lv_img_dsc_t logo;

static lv_obj_t * screens[MAX_SCREENS];

static screen_t current_screen = -1;
static TickType_t current_screen_counter;

static GlobalState * GLOBAL_STATE;

static lv_obj_t *asic_status_label;

static lv_obj_t *hashrate_label;
static lv_obj_t *efficiency_label;
static lv_obj_t *difficulty_label;
static lv_obj_t *chip_temp_label;

static lv_obj_t *firmware_update_scr_filename_label;
static lv_obj_t *firmware_update_scr_status_label;
static lv_obj_t *ip_addr_scr_overheat_label;
static lv_obj_t *ip_addr_scr_urls_label;
static lv_obj_t *mining_url_scr_urls_label;
static lv_obj_t *wifi_status_label;

static lv_obj_t *self_test_message_label;
static lv_obj_t *self_test_result_label;
static lv_obj_t *self_test_finished_label_pass;
static lv_obj_t *self_test_finished_label_fail;

static double current_hashrate;
static float current_power;
static uint64_t current_difficulty;
static float current_chip_temp;
static bool found_block;

#define SCREEN_UPDATE_MS 500
#define LOGO_DELAY_COUNT 5000 / SCREEN_UPDATE_MS
#define CAROUSEL_DELAY_COUNT 10000 / SCREEN_UPDATE_MS

static lv_obj_t * create_scr_self_test() {
    lv_obj_t * scr = lv_obj_create(NULL);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *label1 = lv_label_create(scr);
    lv_label_set_text(label1, "BITAXE SELF TEST");

    self_test_message_label = lv_label_create(scr);
    self_test_result_label = lv_label_create(scr);

    self_test_finished_label_pass = lv_label_create(scr);
    lv_obj_set_width(self_test_finished_label_pass, LV_HOR_RES);
    lv_obj_add_flag(self_test_finished_label_pass, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_long_mode(self_test_finished_label_pass, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(self_test_finished_label_pass, "Press RESET button to start Bitaxe.");

    self_test_finished_label_fail = lv_label_create(scr);
    lv_obj_set_width(self_test_finished_label_fail, LV_HOR_RES);
    lv_obj_add_flag(self_test_finished_label_fail, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_long_mode(self_test_finished_label_fail, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(self_test_finished_label_fail, "Hold BOOT button for 2 seconds to cancel self test, or press RESET to run again.");

    return scr;
}

static lv_obj_t * create_scr_overheat(SystemModule * module) {
    lv_obj_t * scr = lv_obj_create(NULL);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *label1 = lv_label_create(scr);
    lv_label_set_text(label1, "DEVICE OVERHEAT!");

    lv_obj_t *label2 = lv_label_create(scr);
    lv_obj_set_width(label2, LV_HOR_RES);
    lv_label_set_long_mode(label2, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(label2, "Power, frequency and fan configurations have been reset. Go to AxeOS to reconfigure device.");

    lv_obj_t *label3 = lv_label_create(scr);
    lv_label_set_text(label3, "Device IP:");

    ip_addr_scr_overheat_label = lv_label_create(scr);
    lv_label_set_text(ip_addr_scr_overheat_label, module->ip_addr_str);

    return scr;
}

static lv_obj_t * create_scr_asic_status(SystemModule * module) {
    lv_obj_t * scr = lv_obj_create(NULL);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *label1 = lv_label_create(scr);
    lv_label_set_text(label1, "ASIC STATUS:");

    asic_status_label = lv_label_create(scr);
    lv_label_set_long_mode(asic_status_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

    return scr;
}

static lv_obj_t * create_scr_configure(SystemModule * module) {
    lv_obj_t * scr = lv_obj_create(NULL);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *label1 = lv_label_create(scr);
    lv_obj_set_width(label1, LV_HOR_RES);
    lv_obj_set_style_anim_duration(label1, 15000, LV_PART_MAIN);
    lv_label_set_long_mode(label1, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(label1, "Welcome to your new Bitaxe! Connect to the configuration Wi-Fi and connect the Bitaxe to your network.");

    // skip a line, it looks nicer this way
    lv_label_create(scr);

    lv_obj_t *label2 = lv_label_create(scr);
    lv_label_set_text(label2, "Wi-Fi (for setup):");

    lv_obj_t *label3 = lv_label_create(scr);
    lv_label_set_text(label3, module->ap_ssid);

    return scr;
}

static lv_obj_t * create_scr_ota(SystemModule * module) {
    lv_obj_t * scr = lv_obj_create(NULL);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *label1 = lv_label_create(scr);
    lv_obj_set_width(label1, LV_HOR_RES);
    lv_label_set_text(label1, "Firmware update");

    firmware_update_scr_filename_label = lv_label_create(scr);

    firmware_update_scr_status_label = lv_label_create(scr);

    return scr;
}

static lv_obj_t * create_scr_connection(SystemModule * module) {
    lv_obj_t * scr = lv_obj_create(NULL);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *label1 = lv_label_create(scr);
    lv_obj_set_width(label1, LV_HOR_RES);    
    lv_label_set_long_mode(label1, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text_fmt(label1, "Wi-Fi: %s", module->ssid);

    wifi_status_label = lv_label_create(scr);
    lv_label_set_text(wifi_status_label, module->wifi_status);

    lv_obj_t *label3 = lv_label_create(scr);
    lv_label_set_text(label3, "Wi-Fi (for setup):");

    lv_obj_t *label4 = lv_label_create(scr);
    lv_label_set_text(label4, module->ap_ssid);

    return scr;
}

static lv_obj_t * create_scr_logo() {
    lv_obj_t * scr = lv_obj_create(NULL);

    lv_obj_t *img = lv_img_create(scr);
    lv_img_set_src(img, &logo);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);

    return scr;
}

static lv_obj_t * create_scr_urls(SystemModule * module) {
    lv_obj_t * scr = lv_obj_create(NULL);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *label1 = lv_label_create(scr);
    lv_label_set_text(label1, "Stratum Host:");

    mining_url_scr_urls_label = lv_label_create(scr);
    lv_obj_set_width(mining_url_scr_urls_label, LV_HOR_RES);
    lv_label_set_long_mode(mining_url_scr_urls_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(mining_url_scr_urls_label, module->is_using_fallback ? module->fallback_pool_url : module->pool_url);

    lv_obj_t *label3 = lv_label_create(scr);
    lv_label_set_text(label3, "Bitaxe IP:");

    ip_addr_scr_urls_label = lv_label_create(scr);
    lv_label_set_text(ip_addr_scr_urls_label, module->ip_addr_str);

    return scr;
}

static lv_obj_t * create_scr_stats() {
    lv_obj_t * scr = lv_obj_create(NULL);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    hashrate_label = lv_label_create(scr);
    lv_label_set_text(hashrate_label, "Gh/s: --");

    efficiency_label = lv_label_create(scr);
    lv_label_set_text(efficiency_label, "J/Th: --");

    difficulty_label = lv_label_create(scr);
    lv_label_set_text(difficulty_label, "Best: --");

    chip_temp_label = lv_label_create(scr);
    lv_label_set_text(chip_temp_label, "Temp: --");

    return scr;
}

static void screen_show(screen_t screen)
{
    if (current_screen != screen) {
        lv_obj_t * scr = screens[screen];

        if (scr && lvgl_port_lock(0)) {
            lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, LV_DEF_REFR_PERIOD * 128 / 8, 0, false);
            lvgl_port_unlock();
        }

        current_screen = screen;
        current_screen_counter = 0;
    }
}

static void screen_update_cb(lv_timer_t * timer)
{
    if (GLOBAL_STATE->SELF_TEST_MODULE.active) {

        screen_show(SCR_SELF_TEST);

        SelfTestModule * self_test = &GLOBAL_STATE->SELF_TEST_MODULE;

        lv_label_set_text(self_test_message_label, self_test->message);

        if (self_test->finished) {
            if (self_test->result) {
                lv_label_set_text(self_test_result_label, "TESTS PASS!");
                lv_obj_remove_flag(self_test_finished_label_pass, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_label_set_text(self_test_result_label, "TESTS FAIL!");
                lv_obj_remove_flag(self_test_finished_label_fail, LV_OBJ_FLAG_HIDDEN);
            }
        }

        return;
    }

    if (GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update) {
        if (strcmp(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_filename, lv_label_get_text(firmware_update_scr_filename_label)) != 0) {
            lv_label_set_text(firmware_update_scr_filename_label, GLOBAL_STATE->SYSTEM_MODULE.firmware_update_filename);
        }
        if (strcmp(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, lv_label_get_text(firmware_update_scr_status_label)) != 0) {
            lv_label_set_text(firmware_update_scr_status_label, GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status);
        }
        screen_show(SCR_FIRMWARE_UPDATE);
        return;
    }

    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    if (module->asic_status) {
        lv_label_set_text(asic_status_label, module->asic_status);
        screen_show(SCR_ASIC_STATUS);
        return;
    }

    if (module->overheat_mode == 1) {
        if (strcmp(module->ip_addr_str, lv_label_get_text(ip_addr_scr_overheat_label)) != 0) {
            lv_label_set_text(ip_addr_scr_overheat_label, module->ip_addr_str);
        }
        screen_show(SCR_OVERHEAT);
        return;
    }

    if (module->ssid[0] == '\0') {
        screen_show(SCR_CONFIGURE);
        return;
    }

    if (module->ap_enabled) {
        if (strcmp(module->wifi_status, lv_label_get_text(wifi_status_label)) != 0) {
            lv_label_set_text(wifi_status_label, module->wifi_status);
        }
        screen_show(SCR_CONNECTION);
        return;
    }

    current_screen_counter++;

    // Logo

    if (current_screen < SCR_LOGO) {
        screen_show(SCR_LOGO);
        return;
    }

    if (current_screen == SCR_LOGO) {
        if (LOGO_DELAY_COUNT > current_screen_counter) {
            return;
        }
        screen_show(SCR_CAROUSEL_START);
        return;
    }

    // Carousel

    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;

    char *pool_url = module->is_using_fallback ? module->fallback_pool_url : module->pool_url;
    if (strcmp(lv_label_get_text(mining_url_scr_urls_label), pool_url) != 0) {
        lv_label_set_text(mining_url_scr_urls_label, pool_url);
    }

    if (strcmp(lv_label_get_text(ip_addr_scr_urls_label), module->ip_addr_str) != 0) {
        lv_label_set_text(ip_addr_scr_urls_label, module->ip_addr_str);
    }

    if (current_hashrate != module->current_hashrate) {
        lv_label_set_text_fmt(hashrate_label, "Gh/s: %.2f", module->current_hashrate);
    }

    if (current_power != power_management->power || current_hashrate != module->current_hashrate) {
        if (power_management->power > 0 && module->current_hashrate > 0) {
            float efficiency = power_management->power / (module->current_hashrate / 1000.0);
            lv_label_set_text_fmt(efficiency_label, "J/Th: %.2f", efficiency);
        }
    }

    if (module->FOUND_BLOCK && !found_block) {
        found_block = true;

        lv_obj_set_width(difficulty_label, LV_HOR_RES);
        lv_label_set_long_mode(difficulty_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text_fmt(difficulty_label, "Best: %s   !!! BLOCK FOUND !!!", module->best_session_diff_string);

        screen_show(SCR_STATS);
    } else {
        if (current_difficulty != module->best_session_nonce_diff) {
            lv_label_set_text_fmt(difficulty_label, "Best: %s/%s", module->best_session_diff_string, module->best_diff_string);
        }
    }

    if (current_chip_temp != power_management->chip_temp_avg && power_management->chip_temp_avg > 0) {
        lv_label_set_text_fmt(chip_temp_label, "Temp: %.1f C", power_management->chip_temp_avg);
    }

    current_hashrate = module->current_hashrate;
    current_power = power_management->power;
    current_difficulty = module->best_session_nonce_diff;
    current_chip_temp = power_management->chip_temp_avg;

    if (CAROUSEL_DELAY_COUNT > current_screen_counter || found_block) {
        return;
    }

    screen_next();
}

void screen_next()
{
    if (current_screen >= SCR_CAROUSEL_START) {
        screen_show(current_screen == SCR_CAROUSEL_END ? SCR_CAROUSEL_START : current_screen + 1);
    }
}

esp_err_t screen_start(void * pvParameters)
{
    GLOBAL_STATE = (GlobalState *) pvParameters;

    if (GLOBAL_STATE->SYSTEM_MODULE.is_screen_active) {
        SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

        screens[SCR_SELF_TEST] = create_scr_self_test();
        screens[SCR_OVERHEAT] = create_scr_overheat(module);
        screens[SCR_ASIC_STATUS] = create_scr_asic_status(module);
        screens[SCR_CONFIGURE] = create_scr_configure(module);
        screens[SCR_FIRMWARE_UPDATE] = create_scr_ota(module);
        screens[SCR_CONNECTION] = create_scr_connection(module);
        screens[SCR_LOGO] = create_scr_logo();
        screens[SCR_URLS] = create_scr_urls(module);
        screens[SCR_STATS] = create_scr_stats();

        lv_timer_create(screen_update_cb, SCREEN_UPDATE_MS, NULL);
    }

    return ESP_OK;
}
