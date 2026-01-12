/**
 * @file display_config.h
 * @brief Display configuration and variable substitution header
 * @author esp-miner team
 * @date 2025-01-07
 */

#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "global_state.h"

/**
 * @brief Format a display string with variable substitution
 * @param input Input string with {variable} placeholders
 * @param output Buffer for formatted output
 * @param max_len Maximum buffer length
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t display_config_format_string(GlobalState *GLOBAL_STATE, const char *input, char *output, size_t max_len);

/**
 * @brief Get the list of available display variables
 * @param vars Pointer to array of variable names (output)
 * @param count Number of variables (output)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t display_config_get_variables(const char ***vars, size_t *count);

#endif // DISPLAY_CONFIG_H