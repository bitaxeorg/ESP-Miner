/**
 * @file bap.h
 * @brief BAP (Bitaxe accessory protocol) main interface
 * 
 * Main interface for BAP functionality. This header provides the
 * high-level initialization function for the BAP system.
 */

#ifndef BAP_H_
#define BAP_H_

#include "esp_err.h"
#include "global_state.h"
#include "freertos/FreeRTOS.h" 
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Include sub-module headers
#include "bap_protocol.h"
#include "bap_uart.h"
#include "bap_handlers.h"
#include "bap_subscription.h"

extern QueueHandle_t bap_uart_send_queue;
extern SemaphoreHandle_t bap_uart_send_mutex;
extern SemaphoreHandle_t bap_subscription_mutex;
extern GlobalState *bap_global_state;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the complete BAP system
 * 
 * This function initializes all BAP subsystems including:
 * - Protocol utilities
 * - UART communication
 * - Command handlers
 * - Subscription management
 * 
 * @param state Global state pointer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t BAP_init(GlobalState *state);

#ifdef __cplusplus
}
#endif

#endif /* BAP_H_ */
