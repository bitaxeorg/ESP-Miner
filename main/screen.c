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
#define BUTTON_WAKE_MS 5000

#define SCR_CAROUSEL_START SCR_URLS
#define BITAXE_LOGO_INVERTED_SIZE (77 * 30 * 2)
#define OSMU_LOGO_INVERTED_SIZE (125 * 27 * 2)

extern const lv_img_dsc_t bitaxe_logo;
extern const lv_img_dsc_t osmu_logo;
extern const lv_img_dsc_t identify_text;
extern const lv_img_dsc_t bitaxe_background;
extern const lv_font_t ui_font_DigitalNumbers16;
extern const lv_font_t ui_font_DigitalNumbers28;
extern const lv_font_t ui_font_OpenSansBold13;
extern const lv_font_t ui_font_OpenSansBold14;
extern const lv_font_t ui_font_AngelWish40;

static lv_img_dsc_t bitaxe_logo_inverted;
static lv_img_dsc_t osmu_logo_inverted;
static uint8_t bitaxe_logo_inverted_map[BITAXE_LOGO_INVERTED_SIZE];
static uint8_t osmu_logo_inverted_map[OSMU_LOGO_INVERTED_SIZE];
static bool inverted_logos_ready;

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
static lv_obj_t *mining_scriptsig_title_label;
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
static lv_obj_t *stats_fan_rpm_label;
static lv_obj_t *stats_vin_label;
static lv_obj_t *stats_vcore_label;
static lv_obj_t *stats_current_label;
static lv_obj_t *stats_power_label;
static lv_obj_t *stats_uptime_label;
static lv_obj_t *stats_ip_label;
static lv_obj_t *stats_logo_label;

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

static screen_t get_current_screen() {
    lv_obj_t * active_screen = lv_screen_active();
    for (screen_t scr = 0; scr < MAX_SCREENS; scr++) {
        if (screens[scr] == active_screen) return scr;
    }
    return -1;
}

static bool is_large_display(void) {
    return lv_display_get_vertical_resolution(NULL) > 128;
}

static void apply_screen_padding(lv_obj_t * obj) {
    if (is_large_display()) {
        lv_obj_set_style_pad_all(obj, 8, LV_PART_MAIN);
    }
}

static bool use_stats_background(void) {
    return is_large_display();
}

static lv_obj_t * create_positioned_label(
    lv_obj_t * parent,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    const lv_font_t * font,
    const char * text,
    lv_text_align_t align
) {
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
    lv_obj_set_height(label, h);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_pad_all(label, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(label, 0, LV_PART_MAIN);
    return label;
}

static void format_compact_uptime(char * buf, size_t buf_size, int64_t start_time)
{
    uint32_t uptime_seconds = (esp_timer_get_time() - start_time) / 1000000;
    uint32_t days = uptime_seconds / (24 * 3600);
    uptime_seconds %= (24 * 3600);
    uint32_t hours = uptime_seconds / 3600;
    uptime_seconds %= 3600;
    uint32_t minutes = uptime_seconds / 60;
    uptime_seconds %= 60;

    if (days > 0) {
        snprintf(buf, buf_size, "%lud %luh %lum", days, hours, minutes);
    } else if (hours > 0) {
        snprintf(buf, buf_size, "%luh %lum", hours, minutes);
    } else {
        snprintf(buf, buf_size, "%lum %lus", minutes, uptime_seconds);
    }
}

static void make_inverted_logo(const lv_img_dsc_t * src, lv_img_dsc_t * dst, uint8_t * dst_map, size_t dst_map_size) {
    size_t data_size = src->data_size;
    if (data_size > dst_map_size) {
        data_size = dst_map_size;
    }

    *dst = *src;
    dst->data = dst_map;
    dst->data_size = data_size;

    for (size_t i = 0; i + 1 < data_size; i += 2) {
        uint16_t pixel = src->data[i] | (src->data[i + 1] << 8);
        uint16_t inverted_pixel = pixel == 0xffff ? 0x0000 : 0xffff;
        dst_map[i] = inverted_pixel & 0xff;
        dst_map[i + 1] = inverted_pixel >> 8;
    }
}

static void init_inverted_logos(void) {
    if (inverted_logos_ready) {
        return;
    }

    make_inverted_logo(&bitaxe_logo, &bitaxe_logo_inverted, bitaxe_logo_inverted_map, sizeof(bitaxe_logo_inverted_map));
    make_inverted_logo(&osmu_logo, &osmu_logo_inverted, osmu_logo_inverted_map, sizeof(osmu_logo_inverted_map));
    lv_image_cache_drop(&bitaxe_logo);
    lv_image_cache_drop(&osmu_logo);
    lv_image_cache_drop(&bitaxe_logo_inverted);
    lv_image_cache_drop(&osmu_logo_inverted);
    inverted_logos_ready = true;
}

static const lv_img_dsc_t * get_bitaxe_logo_src(void) {
    if (is_large_display()) {
        init_inverted_logos();
        return &bitaxe_logo_inverted;
    }

    return &bitaxe_logo;
}

static const lv_img_dsc_t * get_osmu_logo_src(void) {
    if (is_large_display()) {
        init_inverted_logos();
        return &osmu_logo_inverted;
    }

    return &osmu_logo;
}

static lv_obj_t * create_flex_screen(int expected_lines) {
    lv_obj_t * scr = lv_obj_create(NULL);
    apply_screen_padding(scr);

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
    apply_screen_padding(scr);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(scr, is_large_display() ? 8 : 2, LV_PART_MAIN);

    lv_obj_t * text_cont = lv_obj_create(scr);
    lv_obj_set_style_bg_opa(text_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(text_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(text_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_flex_grow(text_cont, 1);
    lv_obj_set_height(text_cont, LV_VER_RES);

    // Give text a bit more space on larger displays
    if (screen_lines > expected_lines) lv_obj_set_style_pad_row(text_cont, 1, LV_PART_MAIN);

    lv_obj_t * qr = lv_qrcode_create(scr);
    lv_qrcode_set_size(qr, is_large_display() ? 72 : 32);
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
    apply_screen_padding(scr);

    if (is_large_display()) {
        lv_obj_t *logo = lv_label_create(scr);
        lv_label_set_text(logo, "bitaxe");
        lv_obj_set_style_text_font(logo, &ui_font_AngelWish40, LV_PART_MAIN);
        lv_obj_set_style_text_color(logo, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_opa(logo, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_align(logo, LV_ALIGN_CENTER, -42, -16);
    } else {
        lv_obj_t *img = lv_img_create(scr);
        lv_img_set_src(img, get_bitaxe_logo_src());
        lv_obj_align(img, LV_ALIGN_CENTER, 0, 1);
    }

    lv_obj_t *label1 = lv_label_create(scr);
    lv_label_set_text(label1, name);
    lv_obj_align(label1, LV_ALIGN_RIGHT_MID, -8, is_large_display() ? -18 : -12);

    lv_obj_t *label2 = lv_label_create(scr);
    lv_label_set_text(label2, board_version);
    lv_obj_align(label2, LV_ALIGN_RIGHT_MID, -8, is_large_display() ? 2 : -4);

    return scr;
}

static lv_obj_t * create_scr_osmu_logo() {
    lv_obj_t * scr = lv_obj_create(NULL);
    apply_screen_padding(scr);

    lv_obj_t *img = lv_img_create(scr);
    lv_img_set_src(img, get_osmu_logo_src());
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
    if (use_stats_background()) {
        lv_obj_t * scr = lv_obj_create(NULL);
        lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);
        lv_obj_set_style_border_opa(scr, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t * bg = lv_img_create(scr);
        lv_img_set_src(bg, &bitaxe_background);
        lv_obj_align(bg, LV_ALIGN_CENTER, 0, 0);
        lv_obj_move_background(bg);

        stats_logo_label = create_positioned_label(scr, 15, 31, 105, 42, &ui_font_AngelWish40, "Bitaxe", LV_TEXT_ALIGN_LEFT);
        stats_ip_label = create_positioned_label(scr, 94, 1, 100, 13, &ui_font_OpenSansBold13, "--", LV_TEXT_ALIGN_CENTER);
        stats_hashrate_label = create_positioned_label(scr, 15, 126, 98, 36, &ui_font_DigitalNumbers28, "--", LV_TEXT_ALIGN_RIGHT);
        stats_difficulty_label = create_positioned_label(scr, 34, 99, 84, 15, &ui_font_OpenSansBold14, "--", LV_TEXT_ALIGN_LEFT);
        stats_fan_rpm_label = create_positioned_label(scr, 152, 70, 56, 13, &ui_font_OpenSansBold13, "--", LV_TEXT_ALIGN_CENTER);
        stats_temp_label = create_positioned_label(scr, 136, 101, 45, 15, &ui_font_OpenSansBold14, "--", LV_TEXT_ALIGN_RIGHT);
        stats_vin_label = create_positioned_label(scr, 234, 43, 70, 15, &ui_font_OpenSansBold14, "--", LV_TEXT_ALIGN_LEFT);
        stats_vcore_label = create_positioned_label(scr, 234, 65, 70, 15, &ui_font_OpenSansBold14, "--", LV_TEXT_ALIGN_LEFT);
        stats_current_label = create_positioned_label(scr, 234, 87, 70, 15, &ui_font_OpenSansBold14, "--", LV_TEXT_ALIGN_LEFT);
        stats_power_label = create_positioned_label(scr, 234, 109, 70, 15, &ui_font_OpenSansBold14, "--", LV_TEXT_ALIGN_LEFT);
        stats_efficiency_label = create_positioned_label(scr, 217, 136, 60, 20, &ui_font_DigitalNumbers16, "--", LV_TEXT_ALIGN_RIGHT);
        stats_uptime_label = create_positioned_label(scr, 20, 78, 110, 13, &ui_font_OpenSansBold13, "--", LV_TEXT_ALIGN_RIGHT);

        return scr;
    }

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

    mining_scriptsig_title_label = lv_label_create(scr);
    lv_label_set_text(mining_scriptsig_title_label, "Scriptsig:");

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
    lv_obj_set_style_text_color(notification_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(notification_label, LV_ALIGN_TOP_RIGHT, 0, 0);
}

static bool screen_show(screen_t screen)
{
    if (!lvgl_port_lock(0)) {
        return false;
    }

    if (SCR_CAROUSEL_START > screen) {
        lv_display_trigger_activity(NULL);
    }

    bool is_valid = true;
    screen_t current_screen = get_current_screen();
    if (current_screen != screen) {
        lv_obj_t * scr = screens[screen];

        is_valid = lv_obj_is_valid(scr);
        if (is_valid) {
            bool auto_del = current_screen == SCR_BITAXE_LOGO || current_screen == SCR_OSMU_LOGO;
            if (use_stats_background() && screen == SCR_STATS) {
                lv_screen_load(scr);
            } else {
                lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, LV_DEF_REFR_PERIOD * 128 / 8, 0, auto_del);
            }
        }

        current_screen_time_ms = 0;
        current_screen_delay_ms = delays_ms[screen];
    }

    lvgl_port_unlock();

    return is_valid;
}

static void update_stats_background_labels(SystemModule * module, PowerManagementModule * power_management)
{
    if (!use_stats_background()) {
        return;
    }

    lv_label_set_text_fmt(stats_hashrate_label, module->current_hashrate >= 10000.0f ? "%.0f" : "%.1f", module->current_hashrate);

    if (module->ip_addr_str[0] != '\0') {
        lv_label_set_text(stats_ip_label, module->ip_addr_str);
    } else {
        lv_label_set_text(stats_ip_label, "--");
    }

    if (module->best_session_diff_string[0] != '\0') {
        lv_label_set_text(stats_difficulty_label, module->best_session_diff_string);
    } else {
        lv_label_set_text(stats_difficulty_label, "--");
    }

    char uptime[24];
    format_compact_uptime(uptime, sizeof(uptime), module->start_time);
    lv_label_set_text(stats_uptime_label, uptime);

    if (power_management->fan_rpm > 0) {
        lv_label_set_text_fmt(stats_fan_rpm_label, "%u", power_management->fan_rpm);
    } else {
        lv_label_set_text(stats_fan_rpm_label, "--");
    }

    float temp = power_management->chip_temp2_avg > power_management->chip_temp_avg
        ? power_management->chip_temp2_avg
        : power_management->chip_temp_avg;
    if (temp > 0) {
        lv_label_set_text_fmt(stats_temp_label, "%.0f", temp);
    } else {
        lv_label_set_text(stats_temp_label, "--");
    }

    if (power_management->voltage > 0) {
        lv_label_set_text_fmt(stats_vin_label, "%.1fV", power_management->voltage / 1000.0f);
    } else {
        lv_label_set_text(stats_vin_label, "--");
    }

    if (power_management->core_voltage > 0) {
        lv_label_set_text_fmt(stats_vcore_label, "%.2fV", power_management->core_voltage / 1000.0f);
    } else {
        lv_label_set_text(stats_vcore_label, "--");
    }

    if (power_management->current > 0) {
        lv_label_set_text_fmt(stats_current_label, "%.1fA", power_management->current / 1000.0f);
    } else {
        lv_label_set_text(stats_current_label, "--");
    }

    if (power_management->power > 0) {
        lv_label_set_text_fmt(stats_power_label, "%.1fW", power_management->power);
    } else {
        lv_label_set_text(stats_power_label, "--");
    }

    if (power_management->power > 0 && module->current_hashrate > 0) {
        float efficiency = power_management->power / (module->current_hashrate / 1000.0f);
        lv_label_set_text_fmt(stats_efficiency_label, "%.1f", efficiency);
    } else {
        lv_label_set_text(stats_efficiency_label, "--");
    }
}

void screen_next()
{
    if (use_stats_background()) {
        screen_show(SCR_STATS);
        return;
    }

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
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    int32_t display_timeout_config = nvs_config_get_i32(NVS_CONFIG_DISPLAY_TIMEOUT);

    uint32_t inactive_time = lv_display_get_inactive_time(NULL);
    screen_t current_screen = get_current_screen();

    if (module->identify_mode_time_ms > 0) {
        module->identify_mode_time_ms -= SCREEN_UPDATE_MS;
    }
    bool is_identify_mode = module->identify_mode_time_ms > 0;

    bool enable_display = false;
    if (display_timeout_config < 0) {
        // display always on
        enable_display = true;
    } else if (display_timeout_config == 0) {
        // display off, except pre-carousel screens or button press
        if (current_screen < SCR_CAROUSEL_START || inactive_time < BUTTON_WAKE_MS) {
            enable_display = true;
        }
    } else {
        // display timeout
        const uint32_t display_timeout = display_timeout_config * 60 * 1000;

        if (inactive_time < display_timeout || current_screen < SCR_CAROUSEL_START || is_identify_mode) {
            enable_display = true;
        }
    }

    display_on(enable_display);

    if (GLOBAL_STATE->SELF_TEST_MODULE.is_active) {
        SelfTestModule * self_test = &GLOBAL_STATE->SELF_TEST_MODULE;
        
        lv_label_set_text(self_test_message_label, self_test->message);
        
        if (self_test->is_finished) {
            lv_label_set_text(self_test_result_label, self_test->result);
            lv_label_set_text(self_test_finished_label, self_test->finished);
        }
        
        screen_show(SCR_SELF_TEST);
        return;
    }

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

    // Connected display updates

    current_screen_time_ms += SCREEN_UPDATE_MS;

    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;

    if (use_stats_background()) {
        update_stats_background_labels(module, power_management);
        screen_show(SCR_STATS);
        return;
    }

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
        if (module->show_new_block) {
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
            if (power_management->chip_temp2_avg > 0) {
                lv_label_set_text_fmt(stats_temp_label, "Temp: %.1f°C/%.1f°C", power_management->chip_temp_avg, power_management->chip_temp2_avg);
            } else {
                lv_label_set_text_fmt(stats_temp_label, "Temp: %.1f°C", power_management->chip_temp_avg);
            }
        }
        current_chip_temp = power_management->chip_temp_avg;
    }

    update_stats_background_labels(module, power_management);

    if (GLOBAL_STATE->stratum_protocol == STRATUM_V2) {
        // SV2 standard channel: no coinbase data, so no block height or scriptsig
        lv_label_set_text(mining_block_height_label, "Protocol: SV2");
        lv_obj_add_flag(mining_scriptsig_title_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mining_scriptsig_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(mining_scriptsig_title_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(mining_scriptsig_label, LV_OBJ_FLAG_HIDDEN);

        if (current_block_height != GLOBAL_STATE->block_height) {
            lv_label_set_text_fmt(mining_block_height_label, "Block: %d", GLOBAL_STATE->block_height);
            current_block_height = GLOBAL_STATE->block_height;
        }

        if (strcmp(lv_label_get_text(mining_scriptsig_label), GLOBAL_STATE->scriptsig) != 0) {
            lv_label_set_text(mining_scriptsig_label, GLOBAL_STATE->scriptsig);
        }
    }

    if (strcmp(&lv_label_get_text(mining_network_difficulty_label)[9], GLOBAL_STATE->network_diff_string) != 0) {
        lv_label_set_text_fmt(mining_network_difficulty_label, "Difficulty: %s", GLOBAL_STATE->network_diff_string);
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
        lv_label_set_text(notification_label, module->mining_paused ? "▐▐" : "");
    }

    if (module->show_new_block) {
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
    } else if (use_stats_background()) {
        screen_show(SCR_STATS);
        lv_display_trigger_activity(NULL);
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
            snprintf(uptime, sizeof(uptime), "Uptime: %lud %luh %lum %lus", days, hours, minutes, uptime_seconds);
        } else if (hours > 0) {
            snprintf(uptime, sizeof(uptime), "Uptime: %luh %lum %lus", hours, minutes, uptime_seconds);
        } else if (minutes > 0) {
            snprintf(uptime, sizeof(uptime), "Uptime: %lum %lus", minutes, uptime_seconds);
        } else {
            snprintf(uptime, sizeof(uptime), "Uptime: %lus", uptime_seconds);
        }

        if (strcmp(lv_label_get_text(wifi_uptime_label), uptime) != 0) {
            lv_label_set_text(wifi_uptime_label, uptime);
        }
    }
}

esp_err_t screen_start(void * pvParameters)
{
    if (lvgl_port_lock(0)) {
        screen_lines = lv_display_get_vertical_resolution(NULL) / (is_large_display() ? 16 : 8);

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
