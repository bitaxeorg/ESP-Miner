#ifndef DISPLAY_CAPTURE_H
#define DISPLAY_CAPTURE_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/**
 * Initialize the display capture module
 * @return ESP_OK on success, error code on failure
 */
esp_err_t display_capture_init(void);

/**
 * Capture the current display frame and encode to PNG
 * @param png_buffer   Output buffer (will be allocated)
 * @param png_size    Output size
 * @return ESP_OK on success, error code on failure
 */
esp_err_t display_capture_get_png(uint8_t** png_buffer, size_t* png_size);

#endif // DISPLAY_CAPTURE_H
