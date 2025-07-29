/**
 * @file bap.c
 * @brief BAP (Bitcaxe accessory protocol) main interface
 * 
 * Main implementation for BAP functionality. This file provides the
 * high-level initialization function that coordinates all BAP subsystems.
 */

#include "esp_log.h"
#include "bap.h"

static const char *TAG = "BAP";

esp_err_t BAP_init(GlobalState *state) {
    ESP_LOGI(TAG, "Initializing BAP system");
    
    if (!state) {
        ESP_LOGE(TAG, "Invalid global state pointer");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret;
    
    // Initialize subscription management
    ret = BAP_subscription_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize subscription management: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize UART communication
    ret = BAP_uart_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UART: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize command handlers
    ret = BAP_handlers_init(state);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize handlers: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Send initialization message
    BAP_send_init_message(state);
    
    // Start UART receive task
    ret = BAP_start_uart_receive_task();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start UART receive task: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Start subscription task
    ret = BAP_start_subscription_task(state);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start subscription task: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "BAP system initialized successfully");
    return ESP_OK;
}