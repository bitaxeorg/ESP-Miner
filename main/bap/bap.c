/**
 * @file bap.c
 * @brief BAP (Bitaxe accessory protocol) main interface
 * 
 * Main implementation for BAP functionality. This file provides the
 * high-level initialization function that coordinates all BAP subsystems.
 * Also manages shared resources like UART queue and mutexes.
 */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "bap.h"

static const char *TAG = "BAP";

QueueHandle_t bap_uart_send_queue = NULL;
SemaphoreHandle_t bap_uart_send_mutex = NULL;
SemaphoreHandle_t bap_subscription_mutex = NULL;
GlobalState *bap_global_state = NULL;

esp_err_t BAP_init(GlobalState *state) {
    ESP_LOGI(TAG, "Initializing BAP system");
    
    if (!state) {
        ESP_LOGE(TAG, "Invalid global state pointer");
        return ESP_ERR_INVALID_ARG;
    }
    
    bap_global_state = state;
    
    esp_err_t ret;
    
    bap_subscription_mutex = xSemaphoreCreateMutex();
    if (bap_subscription_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create subscription mutex");
        return ESP_ERR_NO_MEM;
    }
    
    bap_uart_send_mutex = xSemaphoreCreateMutex();
    if (bap_uart_send_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create UART send mutex");
        vSemaphoreDelete(bap_subscription_mutex);
        return ESP_ERR_NO_MEM;
    }

    bap_uart_send_queue = xQueueCreateWithCaps(10, sizeof(bap_message_t), MALLOC_CAP_SPIRAM);
    if (bap_uart_send_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create UART send queue");
        vSemaphoreDelete(bap_subscription_mutex);
        vSemaphoreDelete(bap_uart_send_mutex);
        return ESP_ERR_NO_MEM;
    }
    
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
    
    // Start mode-aware BAP management task
    ret = BAP_start_mode_management_task(state);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start BAP mode management task: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "BAP system initialized successfully");
    return ESP_OK;
}