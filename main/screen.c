#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "global_state.h"
#include "screen.h"
#include "nvs_config.h"
#include "display.h"

typedef enum {
    SCR_SELF_TEST,
    SCR_OVERHEAT,
    SCR_ASIC_STATUS,
    SCR_WELCOME,
    SCR_FIRMWARE,
    SCR_CONNECTION,
    SCR_BITAXE_LOGO,
    SCR_OSMU_LOGO,
    SCR_STATUS,
    SCR_INFO,
    MAX_SCREENS,
} screen_t;

#define SCREEN_UPDATE_MS 500
#define SCREEN_INFO_TIMEOUT_MS 30000

#define SCR_NORMAL_START SCR_STATUS

extern const lv_img_dsc_t bitaxe_logo;
extern const lv_img_dsc_t osmu_logo;
extern const lv_font_t lv_font_portfolio_6x8;
extern const lv_font_t font_fmtowns_8x16_data;

static lv_obj_t * screens[MAX_SCREENS];
static int delays_ms[MAX_SCREENS] = {0, 0, 0, 0, 0, 1000, 3000, 3000, 0, SCREEN_INFO_TIMEOUT_MS};

static int current_screen_time_ms;
static int current_screen_delay_ms;

// static int screen_chars;
static int screen_lines;

static GlobalState * GLOBAL_STATE;

static lv_obj_t *self_test_message_label;
static lv_obj_t *self_test_result_label;
static lv_obj_t *self_test_finished_label;

static lv_obj_t *overheat_ip_addr_label;

static lv_obj_t *asic_status_label;

static lv_obj_t *firmware_update_scr_filename_label;
static lv_obj_t *firmware_update_scr_status_label;

static lv_obj_t *connection_wifi_status_label;

static lv_obj_t *status_hashrate_label;
static lv_obj_t *status_best_label;
static lv_obj_t *status_fan_label;
static lv_obj_t *status_temp_label;
static lv_obj_t *status_notification_label;

static lv_obj_t *info_ip_addr_label;

static float current_hashrate = -1.0;
static uint64_t current_difficulty;
static float current_chip_temp = -1.0;
static float current_fan_perc = -1.0;

#define NOTIFICATION_SHARE_ACCEPTED (1 << 0)
#define NOTIFICATION_SHARE_REJECTED (1 << 1)
#define NOTIFICATION_WORK_RECEIVED  (1 << 2)

static const char *notifications[] = {
    "",     // 0b000: NONE
    "↑",    // 0b001:                   ACCEPTED
    "x",    // 0b010:          REJECTED
    "x↑",   // 0b011:          REJECTED ACCEPTED
    "↓",    // 0b100: RECEIVED
    "↕",    // 0b101: RECEIVED          ACCEPTED
    "x↓",   // 0b110: RECEIVED REJECTED 
    "x↕"    // 0b111: RECEIVED REJECTED ACCEPTED
};

static uint64_t current_shares_accepted;
static uint64_t current_shares_rejected;
static uint64_t current_work_received;
static bool self_test_finished;

static screen_t get_current_screen() {
    lv_obj_t * active_screen = lv_screen_active();
    for (screen_t scr = 0; scr < MAX_SCREENS; scr++) {
        if (screens[scr] == active_screen) return scr;
    }
    return -1;
}

static lv_obj_t * create_flex_screen(int expected_lines) {
    lv_obj_t * scr = lv_obj_create(NULL);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    // Give text a bit more space on larger displays
    if (screen_lines > expected_lines) lv_obj_set_style_pad_row(scr, 1, LV_PART_MAIN);

    return scr;
}

static lv_obj_t * create_scr_self_test() {
    lv_obj_t * scr = create_flex_screen(4);

    lv_obj_t *label1 = lv_label_create(scr);
    lv_label_set_text(label1, "BITAXE SELF-TEST");

    self_test_message_label = lv_label_create(scr);
    self_test_result_label = lv_label_create(scr);

    self_test_finished_label = lv_label_create(scr);
    lv_obj_set_width(self_test_finished_label, LV_HOR_RES);
    lv_label_set_long_mode(self_test_finished_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

    return scr;
}

static lv_obj_t * create_scr_overheat(SystemModule * module) {
    lv_obj_t * scr = create_flex_screen(4);

    lv_obj_t *label1 = lv_label_create(scr);
    lv_label_set_text(label1, "DEVICE OVERHEAT!");

    lv_obj_t *label2 = lv_label_create(scr);
    lv_obj_set_width(label2, LV_HOR_RES);
    lv_label_set_long_mode(label2, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(label2, "Power, frequency and fan configurations have been reset. Go to AxeOS to reconfigure device.");

    lv_obj_t *label3 = lv_label_create(scr);
    lv_label_set_text(label3, "IP Address:");

    overheat_ip_addr_label = lv_label_create(scr);

    return scr;
}

static lv_obj_t * create_scr_asic_status(SystemModule * module) {
    lv_obj_t * scr = create_flex_screen(2);

    lv_obj_t *label1 = lv_label_create(scr);
    lv_label_set_text(label1, "ASIC STATUS:");

    asic_status_label = lv_label_create(scr);
    lv_label_set_long_mode(asic_status_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

    return scr;
}

static lv_obj_t * create_screen_with_qr(SystemModule * module, int expected_lines, lv_obj_t ** out_text_cont) {
    lv_obj_t * scr = lv_obj_create(NULL);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(scr, 2, LV_PART_MAIN);

    lv_obj_t * text_cont = lv_obj_create(scr);
    lv_obj_set_flex_flow(text_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_flex_grow(text_cont, 1);
    lv_obj_set_height(text_cont, LV_VER_RES);

    // Give text a bit more space on larger displays
    if (screen_lines > expected_lines) lv_obj_set_style_pad_row(text_cont, 1, LV_PART_MAIN);

    lv_obj_t * qr = lv_qrcode_create(scr);
    lv_qrcode_set_size(qr, 32);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());

    char data[64];
    snprintf(data, sizeof(data), "WIFI:S:%s;;", module->ap_ssid);
    lv_qrcode_update(qr, data, strlen(data));

    *out_text_cont = text_cont;
    return scr;
}

static lv_obj_t * create_scr_welcome(SystemModule * module) {
    lv_obj_t * text_cont;
    lv_obj_t * scr = create_screen_with_qr(module, 3, &text_cont);

    lv_obj_t *label1 = lv_label_create(text_cont);
    lv_obj_set_width(label1, lv_pct(100));
    lv_obj_set_style_anim_duration(label1, 15000, LV_PART_MAIN);
    lv_label_set_long_mode(label1, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(label1, "Welcome to your new Bitaxe! Connect to the configuration Wi-Fi and connect the Bitaxe to your network.");

    // add a bit of padding, it looks nicer this way
    lv_obj_set_style_pad_bottom(label1, 4, LV_PART_MAIN);

    lv_obj_t *label2 = lv_label_create(text_cont);
    lv_label_set_text(label2, "Setup Wi-Fi:");

    lv_obj_t *label3 = lv_label_create(text_cont);
    lv_label_set_text(label3, module->ap_ssid);

    return scr;
}

static lv_obj_t * create_scr_firmware(SystemModule * module) {
    lv_obj_t * scr = create_flex_screen(3);

    lv_obj_t *label1 = lv_label_create(scr);
    lv_obj_set_width(label1, LV_HOR_RES);
    lv_label_set_text(label1, "Firmware update");

    firmware_update_scr_filename_label = lv_label_create(scr);

    firmware_update_scr_status_label = lv_label_create(scr);

    return scr;
}

static lv_obj_t * create_scr_connection(SystemModule * module) {
    lv_obj_t * text_cont;
    lv_obj_t * scr = create_screen_with_qr(module, 4, &text_cont);

    lv_obj_t *label1 = lv_label_create(text_cont);
    lv_obj_set_width(label1, lv_pct(100));
    lv_label_set_long_mode(label1, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text_fmt(label1, "Wi-Fi: %s", module->ssid);

    connection_wifi_status_label = lv_label_create(text_cont);
    lv_obj_set_width(connection_wifi_status_label, lv_pct(100));
    lv_label_set_long_mode(connection_wifi_status_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_obj_t *label3 = lv_label_create(text_cont);
    lv_label_set_text(label3, "Setup Wi-Fi:");

    lv_obj_t *label4 = lv_label_create(text_cont);
    lv_label_set_text(label4, module->ap_ssid);

    return scr;
}

static lv_obj_t * create_scr_bitaxe_logo(const char * name, const char * board_version) {
    lv_obj_t * scr = lv_obj_create(NULL);

    lv_obj_t *img = lv_img_create(scr);
    lv_img_set_src(img, &bitaxe_logo);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 1);

    lv_obj_t *label1 = lv_label_create(scr);
    lv_label_set_text(label1, name);
    lv_obj_align(label1, LV_ALIGN_RIGHT_MID, -6, -12);

    lv_obj_t *label2 = lv_label_create(scr);
    lv_label_set_text(label2, board_version);
    lv_obj_align(label2, LV_ALIGN_RIGHT_MID, -6, -4);

    return scr;
}

static lv_obj_t * create_scr_osmu_logo() {
    lv_obj_t * scr = lv_obj_create(NULL);

    lv_obj_t *img = lv_img_create(scr);
    lv_img_set_src(img, &osmu_logo);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);

    return scr;
}

static lv_obj_t * create_oled_screen_root(void)
{
    lv_obj_t * scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(scr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);

    lv_obj_t * cont = lv_obj_create(scr);
    lv_obj_set_width(cont, 128);
    lv_obj_set_height(cont, 32);
    lv_obj_set_flag(cont, LV_OBJ_FLAG_SCROLLABLE, false);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN);

    return cont;
}

static lv_obj_t * create_oled_label(lv_obj_t * parent, int32_t x, int32_t y, const char * text, const lv_font_t * font)
{
    lv_obj_t * label = lv_label_create(parent);
    lv_obj_set_x(label, x);
    lv_obj_set_y(label, y);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN);

    return label;
}

static lv_obj_t * create_scr_status(void)
{
    lv_obj_t * cont = create_oled_screen_root();
    lv_obj_t * scr = lv_obj_get_parent(cont);

    status_hashrate_label = create_oled_label(cont, 0, 0, "-- TH/s", &font_fmtowns_8x16_data);

    create_oled_label(cont, 0, 16, "BEST", &lv_font_portfolio_6x8);
    status_best_label = create_oled_label(cont, 0, 25, "--", &lv_font_portfolio_6x8);

    create_oled_label(cont, 55, 16, "FAN", &lv_font_portfolio_6x8);
    status_fan_label = create_oled_label(cont, 55, 25, "--%", &lv_font_portfolio_6x8);

    create_oled_label(cont, 100, 16, "TEMP", &lv_font_portfolio_6x8);
    status_temp_label = create_oled_label(cont, 100, 25, "--°C", &lv_font_portfolio_6x8);

    status_notification_label = create_oled_label(cont, 120, 2, "", &lv_font_portfolio_6x8);

    return scr;
}

static lv_obj_t * create_scr_info(void)
{
    lv_obj_t * cont = create_oled_screen_root();
    lv_obj_t * scr = lv_obj_get_parent(cont);

    create_oled_label(cont, 45, 8, "HTTP://", &lv_font_portfolio_6x8);
    info_ip_addr_label = create_oled_label(cont, 5, 18, "---.---.---.---", &font_fmtowns_8x16_data);

    return scr;
}

static bool screen_show(screen_t screen)
{
    if (SCR_NORMAL_START > screen) {
        lv_display_trigger_activity(NULL);
    }

    bool is_valid = true;
    screen_t current_screen = get_current_screen();
    if (current_screen != screen) {
        lv_obj_t * scr = screens[screen];

        is_valid = lv_obj_is_valid(scr);
        if (is_valid && lvgl_port_lock(0)) {
            bool auto_del = current_screen == SCR_BITAXE_LOGO || current_screen == SCR_OSMU_LOGO;
            lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, LV_DEF_REFR_PERIOD * 128 / 8, 0, auto_del);
            lvgl_port_unlock();
        }

        current_screen_time_ms = 0;
        current_screen_delay_ms = delays_ms[screen];
    }
    return is_valid;
}

static void update_status_screen(SystemModule * module, PowerManagementModule * power_management)
{
    if (current_hashrate != module->current_hashrate) {
        lv_label_set_text_fmt(status_hashrate_label, "%.3f TH/s", module->current_hashrate / 1000.0);
        current_hashrate = module->current_hashrate;
    }

    if (current_difficulty != module->best_session_nonce_diff) {
        if (module->block_found) {
            lv_label_set_text(status_best_label, "BLOCK");
        } else {
            lv_label_set_text(status_best_label, module->best_session_diff_string[0] ? module->best_session_diff_string : "--");
        }
        current_difficulty = module->best_session_nonce_diff;
    }

    if (current_fan_perc != power_management->fan_perc) {
        lv_label_set_text_fmt(status_fan_label, "%.0f%%", power_management->fan_perc);
        current_fan_perc = power_management->fan_perc;
    }

    if (current_chip_temp != power_management->chip_temp_avg) {
        if (power_management->chip_temp_avg > 0) {
            lv_label_set_text_fmt(status_temp_label, "%.0f°C", power_management->chip_temp_avg);
        } else {
            lv_label_set_text(status_temp_label, "--°C");
        }
        current_chip_temp = power_management->chip_temp_avg;
    }

    uint32_t shares_accepted = module->shares_accepted;
    uint32_t shares_rejected = module->shares_rejected;
    uint32_t work_received = module->work_received;

    if (current_shares_accepted != shares_accepted
        || current_shares_rejected != shares_rejected
        || current_work_received != work_received) {

        uint8_t state = 0;
        if (shares_accepted > current_shares_accepted) state |= NOTIFICATION_SHARE_ACCEPTED;
        if (shares_rejected > current_shares_rejected) state |= NOTIFICATION_SHARE_REJECTED;
        if (work_received > current_work_received) state |= NOTIFICATION_WORK_RECEIVED;

        lv_label_set_text(status_notification_label, notifications[state]);

        current_shares_accepted = shares_accepted;
        current_shares_rejected = shares_rejected;
        current_work_received = work_received;
    } else if (lv_label_get_text(status_notification_label)[0] != '\0') {
        lv_label_set_text(status_notification_label, "");
    }
}

static void update_info_screen(SystemModule * module)
{
    if (strcmp(module->ip_addr_str, lv_label_get_text(info_ip_addr_label)) != 0) {
        lv_label_set_text(info_ip_addr_label, module->ip_addr_str);
    }
}

static void screen_update_cb(lv_timer_t * timer)
{
    int32_t display_timeout_config = nvs_config_get_i32(NVS_CONFIG_DISPLAY_TIMEOUT);

    if (0 > display_timeout_config) {
        // display always on
        display_on(true);
    } else if (0 == display_timeout_config) {
        // display off
        display_on(false);
    } else {
        // display timeout
        const uint32_t display_timeout = display_timeout_config * 60 * 1000;

        if ((lv_display_get_inactive_time(NULL) > display_timeout) && (SCR_NORMAL_START <= get_current_screen())) {
            display_on(false);
        } else {
            display_on(true);
        }
    }

    if (GLOBAL_STATE->SELF_TEST_MODULE.is_active) {
        SelfTestModule * self_test = &GLOBAL_STATE->SELF_TEST_MODULE;
        
        lv_label_set_text(self_test_message_label, self_test->message);
        
        if (self_test->is_finished && !self_test_finished) {
            self_test_finished = true;
            lv_label_set_text(self_test_result_label, self_test->result);
            lv_label_set_text(self_test_finished_label, self_test->finished);
        }
        
        screen_show(SCR_SELF_TEST);
        return;
    }

    if (GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update) {
        if (strcmp(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_filename, lv_label_get_text(firmware_update_scr_filename_label)) != 0) {
            lv_label_set_text(firmware_update_scr_filename_label, GLOBAL_STATE->SYSTEM_MODULE.firmware_update_filename);
        }
        if (strcmp(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, lv_label_get_text(firmware_update_scr_status_label)) != 0) {
            lv_label_set_text(firmware_update_scr_status_label, GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status);
        }
        screen_show(SCR_FIRMWARE);
        return;
    }

    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    if (module->asic_status) {
        lv_label_set_text(asic_status_label, module->asic_status);

        screen_show(SCR_ASIC_STATUS);
        return;
    }

    if (module->overheat_mode) {
        if (strcmp(module->ip_addr_str, lv_label_get_text(overheat_ip_addr_label)) != 0) {
            lv_label_set_text(overheat_ip_addr_label, module->ip_addr_str);
        }

        screen_show(SCR_OVERHEAT);
        return;
    }

    if (module->ssid[0] == '\0') {
        screen_show(SCR_WELCOME);
        return;
    }

    bool is_wifi_status_changed = strcmp(module->wifi_status, lv_label_get_text(connection_wifi_status_label)) != 0;
    if (module->ap_enabled || is_wifi_status_changed) {
        if (is_wifi_status_changed) {
            lv_label_set_text(connection_wifi_status_label, module->wifi_status);
        }

        screen_show(SCR_CONNECTION);

        delays_ms[SCR_CONNECTION] = 0; // Remove delay so whenever the user disables the AP with long press, it goes straight back to carousel
        return;
    }

    current_screen_time_ms += SCREEN_UPDATE_MS;

    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;

    update_status_screen(module, power_management);
    update_info_screen(module);

    if (module->block_found) {
        lv_display_trigger_activity(NULL);
    }

    screen_t current_screen = get_current_screen();
    if ((current_screen == SCR_BITAXE_LOGO || current_screen == SCR_OSMU_LOGO) && current_screen_time_ms < current_screen_delay_ms) {
        return;
    }

    if (current_screen == SCR_INFO && current_screen_time_ms < current_screen_delay_ms) {
        return;
    }

    screen_show(SCR_STATUS);
}

void screen_next()
{
    if (get_current_screen() == SCR_INFO) {
        screen_show(SCR_STATUS);
    } else {
        screen_show(SCR_INFO);
    }
}

esp_err_t screen_start(void * pvParameters)
{
    if (lvgl_port_lock(0)) {
        // screen_chars = lv_display_get_horizontal_resolution(NULL) / 6;
        screen_lines = lv_display_get_vertical_resolution(NULL) / 8;

        GLOBAL_STATE = (GlobalState *) pvParameters;

        if (GLOBAL_STATE->SYSTEM_MODULE.is_screen_active) {
            SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

            screens[SCR_SELF_TEST] = create_scr_self_test();
            screens[SCR_OVERHEAT] = create_scr_overheat(module);
            screens[SCR_ASIC_STATUS] = create_scr_asic_status(module);
            screens[SCR_WELCOME] = create_scr_welcome(module);
            screens[SCR_FIRMWARE] = create_scr_firmware(module);
            screens[SCR_CONNECTION] = create_scr_connection(module);
            screens[SCR_BITAXE_LOGO] = create_scr_bitaxe_logo(GLOBAL_STATE->DEVICE_CONFIG.family.name, GLOBAL_STATE->DEVICE_CONFIG.board_version);
            screens[SCR_OSMU_LOGO] = create_scr_osmu_logo();
            screens[SCR_STATUS] = create_scr_status();
            screens[SCR_INFO] = create_scr_info();

            lv_timer_create(screen_update_cb, SCREEN_UPDATE_MS, NULL);

            lv_screen_load(screens[SCR_BITAXE_LOGO]);
            current_screen_time_ms = 0;
            current_screen_delay_ms = delays_ms[SCR_BITAXE_LOGO];
        }
        lvgl_port_unlock();
    }

    return ESP_OK;
}
