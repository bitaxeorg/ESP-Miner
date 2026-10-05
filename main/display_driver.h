#ifndef DISPLAY_DRIVER_H_
#define DISPLAY_DRIVER_H_

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"
#include "display.h"

typedef struct DisplayDriver {
    esp_err_t (*init_panel)(GlobalState *state,
                            esp_lcd_panel_io_handle_t *out_io,
                            esp_lcd_panel_handle_t *out_panel,
                            lvgl_port_display_cfg_t *out_disp_cfg);
    esp_err_t (*set_power)(bool enable, esp_lcd_panel_handle_t panel);
    void (*apply_theme)(lv_disp_t *disp);
    bool (*probe)(GlobalState *state);
} DisplayDriver;

#endif /* DISPLAY_DRIVER_H_ */
