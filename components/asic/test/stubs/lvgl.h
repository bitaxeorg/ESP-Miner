#ifndef TEST_STUB_LVGL_H
#define TEST_STUB_LVGL_H

#include <stdint.h>

typedef struct { unsigned unused; } lv_display_t;
typedef lv_display_t lv_disp_t;
typedef enum {
    LV_LOG_LEVEL_TRACE, LV_LOG_LEVEL_INFO, LV_LOG_LEVEL_WARN, LV_LOG_LEVEL_ERROR,
    LV_LOG_LEVEL_USER, LV_LOG_LEVEL_NONE,
} lv_log_level_t;
typedef enum {
    LV_DISPLAY_ROTATION_0, LV_DISPLAY_ROTATION_90,
    LV_DISPLAY_ROTATION_180, LV_DISPLAY_ROTATION_270,
} lv_display_rotation_t;

void lv_log_register_print_cb(void (*callback)(lv_log_level_t, const char *));
lv_display_t *lv_display_create(int32_t width, int32_t height);
void lv_display_set_rotation(lv_display_t *display, lv_display_rotation_t rotation);

#endif
