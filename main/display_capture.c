#include "display_capture.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "stb_png.h"
#include "esp_rom_crc.h"
#include "lvgl.h"
#include "core/lv_refr.h"
#include "display/lv_display.h"
#include "draw/lv_draw.h"
#include "draw/lv_draw_buf.h"

// Private headers for display composition
#include "core/lv_refr_private.h"
#include "display/lv_display_private.h"
#include "core/lv_obj_draw_private.h"

static const char* TAG = "display_capture";

static uint32_t display_width = 128;
static uint32_t display_height = 32;
static uint32_t expected_stride = 16;

// Persistent buffers for streaming (reuse to avoid allocations)
static lv_draw_buf_t* snapshot_buf = NULL;
static uint8_t* output_buf = NULL;
static uint8_t* png_buf = NULL;

esp_err_t display_capture_init(void)
{
    lv_display_t* disp = lv_display_get_default();
    if (disp == NULL) {
        ESP_LOGW(TAG, "No display found");
        return ESP_FAIL;
    }

    display_width = lv_display_get_horizontal_resolution(disp);
    display_height = lv_display_get_vertical_resolution(disp);
    expected_stride = (display_width + 7) / 8;

    ESP_LOGI(TAG, "Display capture initialized: %" PRIu32 "x%" PRIu32, display_width, display_height);

    // Pre-allocate buffers for streaming
    // For 1-bit monochrome, we want a stride that is exactly the byte-aligned width
    snapshot_buf = lv_draw_buf_create(display_width, display_height, LV_COLOR_FORMAT_I1, expected_stride);
    if (snapshot_buf == NULL) {

        ESP_LOGE(TAG, "Failed to create snapshot buffer");
        return ESP_ERR_NO_MEM;
    }

    output_buf = malloc(expected_stride * display_height);
    if (output_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate output buffer");
        lv_draw_buf_destroy(snapshot_buf);
        snapshot_buf = NULL;
        return ESP_ERR_NO_MEM;
    }

    // Pre-allocate PNG buffer (2KB is plenty for up to 128x64 1-bit PNG)
    png_buf = malloc(2048);

    if (png_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate PNG buffer");
        free(output_buf);
        output_buf = NULL;
        lv_draw_buf_destroy(snapshot_buf);
        snapshot_buf = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Buffers pre-allocated: snapshot=%p, output=%p, png=%p", snapshot_buf, output_buf, png_buf);

    return ESP_OK;
}

esp_err_t display_capture_get_png(uint8_t** png_buffer, size_t* png_size)
{
    if (!png_buffer || !png_size) {
        return ESP_ERR_INVALID_ARG;
    }

    *png_buffer = NULL;
    *png_size = 0;

    // Lock LVGL to ensure we get consistent buffer data
    // Use a larger timeout as the system might be busy with mining tasks
    if (!lvgl_port_lock(500)) {
        ESP_LOGW(TAG, "Failed to lock LVGL");
        return ESP_ERR_TIMEOUT;
    }

    // Get the default display
    lv_display_t* disp = lv_display_get_default();
    if (disp == NULL) {
        ESP_LOGW(TAG, "No display found");
        lvgl_port_unlock();
        return ESP_FAIL;
    }

    // Get the active screen
    lv_obj_t* screen = lv_display_get_screen_active(disp);
    if (screen == NULL) {
        ESP_LOGE(TAG, "No active screen");
        lvgl_port_unlock();
        return ESP_FAIL;
    }

    // Manual composition of all layers to capture notifications and transitions
    // Clear draw buffer for fresh snapshot
    lv_draw_buf_clear(snapshot_buf, NULL);

    lv_area_t snapshot_area;
    snapshot_area.x1 = 0;
    snapshot_area.y1 = 0;
    snapshot_area.x2 = display_width - 1;
    snapshot_area.y2 = display_height - 1;

    lv_layer_t layer;
    lv_layer_init(&layer);
    layer.draw_buf = snapshot_buf;
    layer.buf_area = snapshot_area;
    layer.color_format = LV_COLOR_FORMAT_I1;
    layer._clip_area = snapshot_area;
    layer.phy_clip_area = snapshot_area;

    // We must link the layer to the display and set it as refreshing
    // so the draw dispatchers know which display to work on.
    lv_display_t * disp_old = lv_refr_get_disp_refreshing();
    lv_layer_t * layer_old = disp->layer_head;
    disp->layer_head = &layer;
    lv_refr_set_disp_refreshing(disp);

    // Call lv_obj_redraw for each layer of the display in painter's order
    // 1. Bottom layer
    lv_obj_redraw(&layer, lv_display_get_layer_bottom(disp));
    
    // 2. Previous screen (important for transition animations)
    lv_obj_t* prev_scr = lv_display_get_screen_prev(disp);
    if (prev_scr) {
        lv_obj_redraw(&layer, prev_scr);
    }
    
    // 3. Active screen
    lv_obj_redraw(&layer, screen);
    
    // 4. Top layer (notifications, identify image)
    lv_obj_redraw(&layer, lv_display_get_layer_top(disp));
    
    // 5. System layer
    lv_obj_redraw(&layer, lv_display_get_layer_sys(disp));

    // Process all combined draw tasks
    while (layer.draw_task_head) {
        lv_draw_dispatch_wait_for_request();
        lv_draw_dispatch();
    }

    // Restore display state
    disp->layer_head = layer_old;
    lv_refr_set_disp_refreshing(disp_old);

    // Extract the pixel data skipping the palette
    uint32_t buf_stride = snapshot_buf->header.stride;
    uint8_t* src_buf = (uint8_t*)lv_draw_buf_goto_xy(snapshot_buf, 0, 0);

    if (src_buf == NULL) {
        ESP_LOGE(TAG, "Invalid snapshot data");
        lvgl_port_unlock();
        return ESP_FAIL;
    }

    // Copy and invert colors
    for (uint32_t y = 0; y < display_height; y++) {
        const uint8_t* src_row = src_buf + y * buf_stride;
        uint8_t* dst_row = output_buf + y * expected_stride;
        
        for (uint32_t x = 0; x < expected_stride; x++) {
            // Invert colors: LVGL I1 (0=white, 1=black) -> PNG (0=black, 1=white)
            dst_row[x] = ~src_row[x];
        }
    }

    // Unlock LVGL as soon as we're done reading the snapshot buffer
    lvgl_port_unlock();

    // Encode to PNG using pre-allocated reusable buffer
    int png_size_int = 0;

    int ret = stbi_write_png_monochrome_to_mem(output_buf, display_width, display_height, png_buf, &png_size_int);

    if (ret != 0 || png_size_int <= 0) {
        ESP_LOGE(TAG, "PNG encoding failed: %d", ret);
        return ESP_FAIL;
    }

    *png_buffer = png_buf;
    *png_size = (size_t)png_size_int;

    return ESP_OK;
}
