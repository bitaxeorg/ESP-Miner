/**
 * @file bap_handlers.h
 * @brief BAP command handlers
 * 
 * Contains handlers for different BAP commands (REQ, SET, SUB, UNSUB, etc.)
 */

#ifndef BAP_HANDLERS_H_
#define BAP_HANDLERS_H_

#include "esp_err.h"
#include "global_state.h"
#include "bap_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef void (*bap_command_handler_t)(const char *parameter, const char *value);

/**
 * @brief Register a command handler
 * @param cmd Command type to register handler for
 * @param handler Function pointer to handler
 */
void BAP_register_handler(bap_command_t cmd, bap_command_handler_t handler);

/**
 * @brief Parse and dispatch BAP message to appropriate handler
 * @param message Raw BAP message string
 */
void BAP_parse_message(const char *message);

/**
 * @brief Handle subscription request
 * @param parameter Parameter name to subscribe to
 * @param value Optional interval value
 */
void BAP_handle_subscription(const char *parameter, const char *value);

/**
 * @brief Handle unsubscription request
 * @param parameter Parameter name to unsubscribe from
 * @param value Unused
 */
void BAP_handle_unsubscription(const char *parameter, const char *value);

/**
 * @brief Handle parameter request
 * @param parameter Parameter name to request
 * @param value Unused
 */
void BAP_handle_request(const char *parameter, const char *value);

/**
 * @brief Send response for requested parameter
 * @param param Parameter type
 * @param state Global state pointer
 */
void BAP_send_request(bap_parameter_t param, GlobalState *state);

/**
 * @brief Handle settings change
 * @param parameter Parameter name to set
 * @param value New value to set
 */
void BAP_handle_settings(const char *parameter, const char *value);

/**
 * @brief Initialize command handlers and register default handlers
 * @param state Global state pointer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t BAP_handlers_init(GlobalState *state);

#ifdef __cplusplus
}
#endif

#endif /* BAP_HANDLERS_H_ */