/**
 * @file bap_uart.h
 * @brief BAP UART communication layer
 * 
 * Handles UART initialization, message sending/receiving, and communication tasks.
 */

#ifndef BAP_UART_H_
#define BAP_UART_H_

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "global_state.h"
#include "bap_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *message;
    size_t length;
} bap_message_t;

/**
 * @brief Initialize UART for BAP communication
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t BAP_uart_init(void);

/**
 * @brief Send BAP message immediately via UART
 * @param cmd Command type
 * @param parameter Parameter name
 * @param value Value (can be NULL)
 */
void BAP_send_message(bap_command_t cmd, const char *parameter, const char *value);

/**
 * @brief Send BAP message via queue (non-blocking)
 * @param cmd Command type
 * @param parameter Parameter name
 * @param value Value (can be NULL)
 */
void BAP_send_message_with_queue(bap_command_t cmd, const char *parameter, const char *value);

/**
 * @brief Send initialization message
 * @param state Global state (can be NULL)
 */
void BAP_send_init_message(GlobalState *state);

/**
 * @brief Send AP mode message
 * @param state Global state (can be NULL)
 */
void BAP_send_ap_message(GlobalState *state);


/**
 * @brief Start UART receive task
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t BAP_start_uart_receive_task(void);

#ifdef __cplusplus
}
#endif

#endif /* BAP_UART_H_ */