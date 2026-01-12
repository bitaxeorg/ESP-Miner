#include <string.h>
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
#include "default_screens.h"
#include "display_config.h"

typedef enum {
    SCR_SELF_TEST,
    SCR_OVERHEAT,
    SCR_ASIC_STATUS,
    SCR_WELCOME,
    SCR_FIRMWARE,
    SCR_CONNECTION,
    SCR_BITAXE_LOGO,
    SCR_OSMU_LOGO,
    SCR_CAROUSEL,
    MAX_SCREENS,
} screen_t;

#define SCREEN_UPDATE_MS 500

extern const lv_img_dsc_t bitaxe_logo;
extern const lv_img_dsc_t osmu_logo;
extern const lv_img_dsc_t identify_text;

static lv_obj_t * screens[MAX_SCREENS];
static screen_t current_screen;

static int delays_ms[MAX_SCREENS] = {0, 0, 0, 0, 0, 1000, 3000, 3000, 10000};

static int current_screen_time_ms;
static int current_screen_delay_ms;
static int current_carousel_index = 0;

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

static lv_obj_t *notification_label;
static lv_obj_t *identify_image;

static lv_obj_t *carousel_labels[MAX_CAROUSEL_LABELS] = {0};

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

static void update_carousel_screen_content(int screen_index, lv_obj_t *labels[MAX_CAROUSEL_LABELS])
{
    // Get custom screen content from NVS
    char *screen_content = nvs_config_get_string_indexed(NVS_CONFIG_SCREENS, screen_index);
    const char *content = screen_content;

    // If empty or not found, use default
    if (!content || content[0] == '\0') {
        free(screen_content);
        screen_content = NULL;
        if (screen_index < DEFAULT_SCREENS_COUNT) {
            content = default_screens[screen_index];
        } else {
            content = "";
        }
    }

    char *content_copy = strdup(content);
    if (content_copy) {
        char *line = strtok(content_copy, "\n");
        int line_count = 0;

        while (line && line_count < MAX_CAROUSEL_LABELS && labels[line_count]) {
            char formatted_line[128];
            if (display_config_format_string(GLOBAL_STATE, line, formatted_line, sizeof(formatted_line)) == ESP_OK) {
                lv_label_set_text(labels[line_count], formatted_line);
            } else {
                lv_label_set_text(labels[line_count], line);
            }
            line = strtok(NULL, "\n");
            line_count++;
        }

        // Clear any remaining labels
        for (int i = line_count; i < MAX_CAROUSEL_LABELS; i++) {
            if (labels[i]) {
                lv_label_set_text(labels[i], "");
            }
        }

        free(content_copy);
    }

    free(screen_content);
}

static lv_obj_t * create_scr_carousel(int screen_index)
{
    char *screen_content = nvs_config_get_string_indexed(NVS_CONFIG_SCREENS, screen_index);
    const char *content = screen_content;

    if (!content || content[0] == '\0') {
        free(screen_content);
        screen_content = NULL;
        if (screen_index < DEFAULT_SCREENS_COUNT) {
            content = default_screens[screen_index];
        } else {
            content = "";
        }
    }

    // Truly empty → caller will skip this page
    if (!content || content[0] == '\0') {
        free(screen_content);
        return NULL;
    }

    // Count lines including blank ones
    int total_lines = 1;
    for (const char *p = content; *p; p++) {
        if (*p == '\n') total_lines++;
    }

    int lines_to_show = total_lines;
    if (lines_to_show > screen_lines) lines_to_show = screen_lines;
    if (lines_to_show > MAX_CAROUSEL_LABELS) lines_to_show = MAX_CAROUSEL_LABELS;

    lv_obj_t *scr = create_flex_screen(lines_to_show);

    memset(carousel_labels, 0, sizeof(carousel_labels));

    for (int i = 0; i < lines_to_show; i++) {
        lv_obj_t *label = lv_label_create(scr);
        lv_obj_set_width(label, LV_HOR_RES);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        carousel_labels[i] = label;
    }

    update_carousel_screen_content(screen_index, carousel_labels);

    free(screen_content);
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
    if (screen < SCR_CAROUSEL) {
        lv_display_trigger_activity(NULL);
    }

    bool is_valid = true;
    if (current_screen != screen || screen == SCR_CAROUSEL) {
        lv_obj_t *scr = NULL;
        if (screen == SCR_CAROUSEL) {
            scr = create_scr_carousel(current_carousel_index);
            if (!scr) {
                return false;
            }
        } else {
            scr = screens[screen];
        }

        is_valid = lv_obj_is_valid(scr);
        if (is_valid && lvgl_port_lock(0)) {
            bool auto_del = (current_screen == SCR_BITAXE_LOGO || current_screen == SCR_OSMU_LOGO || current_screen == SCR_CAROUSEL);
            lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, LV_DEF_REFR_PERIOD * 128 / 8, 0, auto_del);
            current_screen = screen;
            lvgl_port_unlock();
        }

        current_screen_time_ms = 0;
        current_screen_delay_ms = delays_ms[screen];
    }
    return is_valid;
}

void screen_next()
{
    if (current_screen < SCR_CAROUSEL) {
        screen_t next = current_screen + 1;
        while (next < SCR_CAROUSEL) {
            if (screen_show(next)) return;
            next++;
        }
        current_carousel_index = 0;
        screen_show(SCR_CAROUSEL);
        return;
    }

    int start_index = current_carousel_index;

    do {
        current_carousel_index = (current_carousel_index + 1) % MAX_CAROUSEL_SCREENS;

        if (screen_show(SCR_CAROUSEL) || current_carousel_index == start_index) {
            current_screen_time_ms = 0;
            return;
        }
    } while (true);
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

        if ((lv_display_get_inactive_time(NULL) > display_timeout) && (current_screen == SCR_CAROUSEL) &&
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

    // Always update content in case variables changed
    if (current_screen == SCR_CAROUSEL) {
        update_carousel_screen_content(current_carousel_index, carousel_labels);
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
        if (current_screen != SCR_CAROUSEL || current_carousel_index != 1) {
            current_carousel_index = 1;  // Stats screen
            screen_show(SCR_CAROUSEL);
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
            screens[SCR_CAROUSEL] = NULL;  // Created dynamically

            scr_create_overlay();

            lv_timer_create(screen_update_cb, SCREEN_UPDATE_MS, NULL);
        }
        lvgl_port_unlock();
    }

    return ESP_OK;
}
