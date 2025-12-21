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
#include "connect.h"
#include "esp_timer.h"

typedef enum {
    SCR_SELF_TEST,
    SCR_OVERHEAT,
    SCR_ASIC_STATUS,
    SCR_WELCOME,
    SCR_FIRMWARE,
    SCR_CONNECTION,
    SCR_BITAXE_LOGO,
    SCR_OSMU_LOGO,
    SCR_URLS,
    SCR_STATS,
    SCR_MINING,
    SCR_WIFI,
    MAX_SCREENS,
} screen_t;

#define SCREEN_UPDATE_MS 500

#define SCR_CAROUSEL_START SCR_URLS

extern const lv_img_dsc_t bitaxe_logo;
extern const lv_img_dsc_t osmu_logo;
extern const lv_img_dsc_t identify_text;

static lv_obj_t * screens[MAX_SCREENS];
static int delays_ms[MAX_SCREENS] = {0, 0, 0, 0, 0, 1000, 3000, 3000, 10000, 10000, 10000, 10000};

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

static lv_obj_t *mining_block_height_label;
static lv_obj_t *mining_network_difficulty_label;
static lv_obj_t *mining_scriptsig_label;

static lv_obj_t *firmware_update_scr_filename_label;
static lv_obj_t *firmware_update_scr_status_label;

static lv_obj_t *connection_wifi_status_label;

static lv_obj_t *urls_ip_addr_label;
static lv_obj_t *urls_mining_url_label;

static lv_obj_t *stats_hashrate_label;
static lv_obj_t *stats_efficiency_label;
static lv_obj_t *stats_difficulty_label;
static lv_obj_t *stats_temp_label;

static lv_obj_t *wifi_rssi_value_label;
static lv_obj_t *wifi_signal_strength_label;
static lv_obj_t *wifi_uptime_label;

static lv_obj_t *notification_label;
static lv_obj_t *identify_image;

static float current_hashrate;
static float current_power;
static uint64_t current_difficulty;
static float current_chip_temp;

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
static int8_t current_rssi_value;
static int current_block_height;

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

static lv_obj_t * create_scr_overheat() {
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

static lv_obj_t * create_scr_asic_status() {
    lv_obj_t * scr = create_flex_screen(2);

    lv_obj_t *label1 = lv_label_create(scr);
    lv_label_set_text(label1, "ASIC STATUS:");

    asic_status_label = lv_label_create(scr);
    lv_label_set_long_mode(asic_status_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

    return scr;
}

static lv_obj_t * create_screen_with_qr(const char * ap_ssid, int expected_lines, lv_obj_t ** out_text_cont) {
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
    snprintf(data, sizeof(data), "WIFI:S:%s;;", ap_ssid);
    lv_qrcode_update(qr, data, strlen(data));

    *out_text_cont = text_cont;
    return scr;
}

static lv_obj_t * create_scr_welcome(const char * ap_ssid) {
    lv_obj_t * text_cont;
    lv_obj_t * scr = create_screen_with_qr(ap_ssid, 3, &text_cont);

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
    lv_label_set_text(label3, ap_ssid);

    return scr;
}

static lv_obj_t * create_scr_firmware() {
    lv_obj_t * scr = create_flex_screen(3);

    lv_obj_t *label1 = lv_label_create(scr);
    lv_obj_set_width(label1, LV_HOR_RES);
    lv_label_set_text(label1, "Firmware update");

    firmware_update_scr_filename_label = lv_label_create(scr);

    firmware_update_scr_status_label = lv_label_create(scr);

    return scr;
}

static lv_obj_t * create_scr_connection(const char * ssid, const char * ap_ssid) {
    lv_obj_t * text_cont;
    lv_obj_t * scr = create_screen_with_qr(ap_ssid, 4, &text_cont);

    lv_obj_t *label1 = lv_label_create(text_cont);
    lv_obj_set_width(label1, lv_pct(100));
    lv_label_set_long_mode(label1, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text_fmt(label1, "Wi-Fi: %s", ssid);

    connection_wifi_status_label = lv_label_create(text_cont);
    lv_obj_set_width(connection_wifi_status_label, lv_pct(100));
    lv_label_set_long_mode(connection_wifi_status_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_obj_t *label3 = lv_label_create(text_cont);
    lv_label_set_text(label3, "Setup Wi-Fi:");

    lv_obj_t *label4 = lv_label_create(text_cont);
    lv_label_set_text(label4, ap_ssid);

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

static lv_obj_t * create_scr_urls() {
    lv_obj_t * scr = create_flex_screen(4);

    lv_obj_t *label1 = lv_label_create(scr);
    lv_label_set_text(label1, "Stratum Host:");

    urls_mining_url_label = lv_label_create(scr);
    lv_obj_set_width(urls_mining_url_label, LV_HOR_RES);
    lv_label_set_long_mode(urls_mining_url_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_obj_t *label3 = lv_label_create(scr);
    lv_label_set_text(label3, "IP Address:");

    urls_ip_addr_label = lv_label_create(scr);

    return scr;
}

static lv_obj_t * create_scr_stats() {
    lv_obj_t * scr = create_flex_screen(4);

    stats_hashrate_label = lv_label_create(scr);
    lv_label_set_text(stats_hashrate_label, "Gh/s: --");

    stats_efficiency_label = lv_label_create(scr);
    lv_label_set_text(stats_efficiency_label, "J/Th: --");

    stats_difficulty_label = lv_label_create(scr);
    lv_label_set_text(stats_difficulty_label, "Best: --");

    stats_temp_label = lv_label_create(scr);
    lv_label_set_text(stats_temp_label, "Temp: --");

    return scr;
}

static lv_obj_t * create_scr_mining() {
    lv_obj_t * scr = create_flex_screen(4);

    mining_block_height_label = lv_label_create(scr);
    lv_label_set_text(mining_block_height_label, "Block: --");

    mining_network_difficulty_label = lv_label_create(scr);
    lv_label_set_text(mining_network_difficulty_label, "Difficulty: --");

    lv_obj_t *label3 = lv_label_create(scr);
    lv_label_set_text(label3, "Scriptsig:");

    mining_scriptsig_label = lv_label_create(scr);
    lv_label_set_text(mining_scriptsig_label, "--");
    lv_obj_set_width(mining_scriptsig_label, LV_HOR_RES);
    lv_label_set_long_mode(mining_scriptsig_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

    return scr;
}

static lv_obj_t * create_scr_wifi() {
    lv_obj_t * scr = create_flex_screen(4);

    lv_obj_t *title_label = lv_label_create(scr);
    lv_label_set_text(title_label, "Wi-Fi Signal");

    wifi_rssi_value_label = lv_label_create(scr);
    lv_label_set_text(wifi_rssi_value_label, "RSSI: -- dBm");

    wifi_signal_strength_label = lv_label_create(scr);
    lv_label_set_text(wifi_signal_strength_label, "Signal: --%%");

    wifi_uptime_label = lv_label_create(scr);
    lv_label_set_text(wifi_uptime_label, "Uptime: --");

    return scr;
}

static void scr_create_overlay()
{
    identify_image = lv_img_create(lv_layer_top());
    lv_img_set_src(identify_image, &identify_text);
    lv_obj_align(identify_image, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(identify_image, LV_OBJ_FLAG_HIDDEN);

    notification_label = lv_label_create(lv_layer_top());
    lv_label_set_text(notification_label, "");
    lv_obj_align(notification_label, LV_ALIGN_TOP_RIGHT, 0, 0);
}

static bool screen_show(screen_t screen)
{
    if (SCR_CAROUSEL_START > screen) {
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

void screen_next()
{
    screen_t next_scr = get_current_screen();
    do {
        next_scr++;

        if (next_scr == MAX_SCREENS) {
            next_scr = SCR_CAROUSEL_START;
        }

    } while (!screen_show(next_scr));
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

        if ((lv_display_get_inactive_time(NULL) > display_timeout) && (SCR_CAROUSEL_START <= get_current_screen()) &&
             lv_obj_has_flag(identify_image, LV_OBJ_FLAG_HIDDEN)) {
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

    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    if (module->identify_mode_time_ms > 0) {
        module->identify_mode_time_ms -= SCREEN_UPDATE_MS;
    }

    bool is_identify_mode = module->identify_mode_time_ms > 0;
    if (is_identify_mode == lv_obj_has_flag(identify_image, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_set_flag(identify_image, LV_OBJ_FLAG_HIDDEN, !is_identify_mode);
        lv_obj_set_style_bg_opa(lv_layer_top(), is_identify_mode ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_color(lv_layer_top(), is_identify_mode ? lv_color_black() : lv_color_white(), LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(notification_label, is_identify_mode ? lv_color_white() : lv_color_black(), LV_PART_MAIN);
    }

    if (module->is_firmware_update) {
        if (strcmp(module->firmware_update_filename, lv_label_get_text(firmware_update_scr_filename_label)) != 0) {
            lv_label_set_text(firmware_update_scr_filename_label, module->firmware_update_filename);
        }
        if (strcmp(module->firmware_update_status, lv_label_get_text(firmware_update_scr_status_label)) != 0) {
            lv_label_set_text(firmware_update_scr_status_label, module->firmware_update_status);
        }
        screen_show(SCR_FIRMWARE);
        return;
    }

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
    if (is_wifi_status_changed) {
        lv_label_set_text(connection_wifi_status_label, module->wifi_status);
        screen_show(SCR_CONNECTION);
        return;
    }

    if (module->ap_enabled || !module->is_connected) {
        screen_show(SCR_CONNECTION);
        return;
    }

    // Carousel

    current_screen_time_ms += SCREEN_UPDATE_MS;

    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;

    char *pool_url = module->is_using_fallback ? module->fallback_pool_url : module->pool_url;
    if (strcmp(lv_label_get_text(urls_mining_url_label), pool_url) != 0) {
        lv_label_set_text(urls_mining_url_label, pool_url);
    }

    if (strcmp(lv_label_get_text(urls_ip_addr_label), module->ip_addr_str) != 0) {
        lv_label_set_text(urls_ip_addr_label, module->ip_addr_str);
    }

    if (current_hashrate != module->current_hashrate) {
        lv_label_set_text_fmt(stats_hashrate_label, "Gh/s: %.2f", module->current_hashrate);
    }

    if (current_power != power_management->power || current_hashrate != module->current_hashrate) {
        if (power_management->power > 0 && module->current_hashrate > 0) {
            float efficiency = power_management->power / (module->current_hashrate / 1000.0);
            lv_label_set_text_fmt(stats_efficiency_label, "J/Th: %.2f", efficiency);
        }
        current_power = power_management->power;
    }
    current_hashrate = module->current_hashrate;

    if (current_difficulty != module->best_session_nonce_diff) {
        if (module->block_found) {
            lv_obj_set_width(stats_difficulty_label, LV_HOR_RES);
            lv_label_set_long_mode(stats_difficulty_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_label_set_text_fmt(stats_difficulty_label, "Best: %s   !!! BLOCK FOUND !!!", module->best_session_diff_string);
        } else {
            lv_label_set_text_fmt(stats_difficulty_label, "Best: %s/%s", module->best_session_diff_string, module->best_diff_string);
        }
        current_difficulty = module->best_session_nonce_diff;
    }

    if (current_chip_temp != power_management->chip_temp_avg) {
        if (power_management->chip_temp_avg > 0) {
            lv_label_set_text_fmt(stats_temp_label, "Temp: %.1f°C", power_management->chip_temp_avg);    
        }
        current_chip_temp = power_management->chip_temp_avg;
    }

    if (current_block_height != GLOBAL_STATE->block_height) {
        lv_label_set_text_fmt(mining_block_height_label, "Block: %d", GLOBAL_STATE->block_height);
        current_block_height = GLOBAL_STATE->block_height;
    }
    
    if (strcmp(&lv_label_get_text(mining_network_difficulty_label)[9], GLOBAL_STATE->network_diff_string) != 0) {
        lv_label_set_text_fmt(mining_network_difficulty_label, "Difficulty: %s", GLOBAL_STATE->network_diff_string);
    }

    if (strcmp(lv_label_get_text(mining_scriptsig_label), GLOBAL_STATE->scriptsig) != 0) {
        lv_label_set_text(mining_scriptsig_label, GLOBAL_STATE->scriptsig);
    }

    // Update WiFi RSSI periodically
    int8_t rssi_value = -128;
    if (module->is_connected) {
        get_wifi_current_rssi(&rssi_value);
    }

    if (rssi_value != current_rssi_value) {
        if (rssi_value > -50) {
            lv_label_set_text(wifi_signal_strength_label, "Signal: Excellent");
        } else if (rssi_value > -60) {
            lv_label_set_text(wifi_signal_strength_label, "Signal: Good");
        } else if (rssi_value > -70) {
            lv_label_set_text(wifi_signal_strength_label, "Signal: Fair");
        } else if (rssi_value > -128){
            lv_label_set_text(wifi_signal_strength_label, "Signal: Weak");
        } else {
            lv_label_set_text(wifi_signal_strength_label, "Signal: --");
        }

        if (rssi_value > -128) {
            lv_label_set_text_fmt(wifi_rssi_value_label, "RSSI: %d dBm", rssi_value);
        } else {
            lv_label_set_text(wifi_rssi_value_label, "RSSI: -- dBm");
        }
        current_rssi_value = rssi_value;
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

        lv_label_set_text(notification_label, notifications[state]);

        current_shares_accepted = shares_accepted;
        current_shares_rejected = shares_rejected;
        current_work_received = work_received;
    } else {
        lv_label_set_text(notification_label, "");
    }

    if (module->block_found) {
        if (get_current_screen() != SCR_STATS) {
            screen_show(SCR_STATS);
        }

        lv_display_trigger_activity(NULL);
        return;
    }

    if (current_screen_time_ms <= current_screen_delay_ms) {
        return;
    }

    screen_next();
}

void screen_button_press() 
{
    if (GLOBAL_STATE->SYSTEM_MODULE.identify_mode_time_ms > 0) {
        GLOBAL_STATE->SYSTEM_MODULE.identify_mode_time_ms = 0;
    } else {
        screen_next();
    }
}

static void uptime_update_cb(lv_timer_t * timer)
{
    if (wifi_uptime_label) {
        char uptime[50];
        uint32_t uptime_seconds = (esp_timer_get_time() - GLOBAL_STATE->SYSTEM_MODULE.start_time) / 1000000;

        uint32_t days = uptime_seconds / (24 * 3600);
        uptime_seconds %= (24 * 3600);
        uint32_t hours = uptime_seconds / 3600;
        uptime_seconds %= 3600;
        uint32_t minutes = uptime_seconds / 60;
        uptime_seconds %= 60;

        if (days > 0) {
            snprintf(uptime, sizeof(uptime), "Uptime: %ldd %ldh %ldm %lds", days, hours, minutes, uptime_seconds);
        } else if (hours > 0) {
            snprintf(uptime, sizeof(uptime), "Uptime: %ldh %ldm %lds", hours, minutes, uptime_seconds);
        } else if (minutes > 0) {
            snprintf(uptime, sizeof(uptime), "Uptime: %ldm %lds", minutes, uptime_seconds);
        } else {
            snprintf(uptime, sizeof(uptime), "Uptime: %lds", uptime_seconds);
        }

        if (strcmp(lv_label_get_text(wifi_uptime_label), uptime) != 0) {
            lv_label_set_text(wifi_uptime_label, uptime);
        }
    }
}

esp_err_t screen_start(void * pvParameters)
{
    if (lvgl_port_lock(0)) {
        // screen_chars = lv_display_get_horizontal_resolution(NULL) / 6;
        screen_lines = lv_display_get_vertical_resolution(NULL) / 8;

        GLOBAL_STATE = (GlobalState *) pvParameters;
        SystemModule * SYSTEM_MODULE = &GLOBAL_STATE->SYSTEM_MODULE;

        if (SYSTEM_MODULE->is_screen_active) {

            screens[SCR_SELF_TEST] = create_scr_self_test();
            screens[SCR_OVERHEAT] = create_scr_overheat();
            screens[SCR_ASIC_STATUS] = create_scr_asic_status();
            screens[SCR_WELCOME] = create_scr_welcome(SYSTEM_MODULE->ap_ssid);
            screens[SCR_FIRMWARE] = create_scr_firmware();
            screens[SCR_CONNECTION] = create_scr_connection(SYSTEM_MODULE->ssid, SYSTEM_MODULE->ap_ssid);
            screens[SCR_BITAXE_LOGO] = create_scr_bitaxe_logo(GLOBAL_STATE->DEVICE_CONFIG.family.name, GLOBAL_STATE->DEVICE_CONFIG.board_version);
            screens[SCR_OSMU_LOGO] = create_scr_osmu_logo();
            screens[SCR_URLS] = create_scr_urls();
            screens[SCR_STATS] = create_scr_stats();
            screens[SCR_MINING] = create_scr_mining();
            screens[SCR_WIFI] = create_scr_wifi();

            scr_create_overlay();

            lv_timer_create(screen_update_cb, SCREEN_UPDATE_MS, NULL);

            // Create uptime update timer (runs every 1 second)
            lv_timer_create(uptime_update_cb, 1000, NULL);
        }
        lvgl_port_unlock();
    }

    return ESP_OK;
}
