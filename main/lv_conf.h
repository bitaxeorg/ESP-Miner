#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

#define LV_COLOR_DEPTH 1

#define LV_USE_OS LV_OS_FREERTOS

#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED         0
#define LV_DRAW_SW_SUPPORT_RGB565A8               0
#define LV_DRAW_SW_SUPPORT_RGB888                 0
#define LV_DRAW_SW_SUPPORT_XRGB8888               0
#define LV_DRAW_SW_SUPPORT_ARGB8888               1
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
#define LV_DRAW_SW_SUPPORT_L8                     0
#define LV_DRAW_SW_SUPPORT_AL88                   0
#define LV_DRAW_SW_SUPPORT_A8                     0

#define LV_DRAW_SW_COMPLEX 1

#define LV_USE_FLOAT 1

#define LV_FONT_MONTSERRAT_14 0

#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(lv_font_portfolio_6x8)
#define LV_FONT_DEFAULT        &lv_font_portfolio_6x8

#define LV_USE_FONT_PLACEHOLDER 0

#define LV_WIDGETS_HAS_DEFAULT_VALUE 0

#define LV_USE_ANIMIMG      0
#define LV_USE_ARC          0
#define LV_USE_BAR          0
#define LV_USE_BUTTON       0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR     0
#define LV_USE_CANVAS       1
#define LV_USE_CHART        0
#define LV_USE_CHECKBOX     0
#define LV_USE_DROPDOWN     0
#define LV_USE_IMAGEBUTTON  0
#define LV_USE_KEYBOARD     0
#define LV_USE_LED          0
#define LV_USE_LINE         0
#define LV_USE_LIST         0
#define LV_USE_MENU         0
#define LV_USE_MSGBOX       0
#define LV_USE_ROLLER       0
#define LV_USE_SCALE        0
#define LV_USE_SLIDER       0
#define LV_USE_SPAN         0
#define LV_USE_SPINBOX      0
#define LV_USE_SPINNER      0
#define LV_USE_SWITCH       0
#define LV_USE_TABLE        0
#define LV_USE_TABVIEW      0
#define LV_USE_TEXTAREA     0
#define LV_USE_TILEVIEW     0
#define LV_USE_WIN          0

#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE  0
#define LV_USE_THEME_MONO    0

#define LV_USE_QRCODE 1

#define LV_USE_OBSERVER 0

#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS    0

#define LV_MEM_POOL_INCLUDE     "esp_heap_caps.h"
#define LV_MEM_POOL_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA)

#endif /* LV_CONF_H */
