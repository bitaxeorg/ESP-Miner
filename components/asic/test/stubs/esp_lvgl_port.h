#ifndef TEST_STUB_ESP_LVGL_PORT_H
#define TEST_STUB_ESP_LVGL_PORT_H

#include <stdbool.h>
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "lvgl.h"

typedef struct { uint32_t task_stack_caps; } lvgl_port_cfg_t;
typedef struct { unsigned unused; } lvgl_port_display_cfg_t;
#define ESP_LVGL_PORT_INIT_CONFIG() ((lvgl_port_cfg_t){0})

esp_err_t lvgl_port_init(const lvgl_port_cfg_t *config);
bool lvgl_port_lock(uint32_t timeout_ms);
void lvgl_port_unlock(void);
lv_display_t *lvgl_port_add_disp(const lvgl_port_display_cfg_t *config);

#endif
