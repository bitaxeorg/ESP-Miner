#ifndef BAP_H_
#define BAP_H_

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "global_state.h"
#include "driver/uart.h"

// BAP Command Types
typedef enum {
    BAP_CMD_REQ,
    BAP_CMD_RES,
    BAP_CMD_SUB,
    BAP_CMD_UNSUB,
    BAP_CMD_SET,
    BAP_CMD_ACK,
    BAP_CMD_ERR,
    BAP_CMD_CMD,
    BAP_CMD_STA,
    BAP_CMD_LOG,
    BAP_CMD_UNKNOWN
} bap_command_t;

// Subscription parameter types
typedef enum {
    BAP_PARAM_SYSTEM_INFO,
    BAP_PARAM_HASHRATE,
    BAP_PARAM_TEMPERATURE,
    BAP_PARAM_POWER,
    BAP_PARAM_VOLTAGE,
    BAP_PARAM_CURRENT,
    BAP_PARAM_SHARES,
    BAP_PARAM_FREQUENCY,
    BAP_PARAM_ASIC_VOLTAGE,
    BAP_PARAM_SSID,
    BAP_PARAM_PASSWORD,
    BAP_PARAM_UNKNOWN
} bap_parameter_t;

// Subscription status
typedef struct {
    bool active;
    uint32_t last_response;         // When last subscription message was sent
    uint32_t update_interval_ms;
    uint32_t last_subscribe;        // When subscription was last subscritpion renewed
} bap_subscription_t;

// Message queue structure
typedef struct {
    char *message;
    size_t length;
} bap_message_t;

// Callback Type
typedef void (*bap_command_handler_t)(const char *parameter, const char *value);

// Function declarations
esp_err_t BAP_init(void);
void BAP_send_message(bap_command_t cmd, const char *parameter, const char *value);
void BAP_send_message_with_queue(bap_command_t cmd, const char *parameter, const char *value);
void BAP_register_handler(bap_command_t cmd, bap_command_handler_t handler);
void BAP_parse_message(const char *message);
void BAP_send_init_message(GlobalState *state);
void BAP_handle_subscription(const char *parameter, const char *value);
void BAP_handle_unsubscription(const char *parameter, const char *value);
void BAP_handle_request(const char *parameter, const char *value);
void BAP_send_request(bap_parameter_t param, GlobalState *state);
void BAP_handle_settings(const char *parameter, const char *value);
void BAP_send_subscription_update(GlobalState *state);
esp_err_t BAP_start_uart_receive_task(void);
esp_err_t BAP_start_subscription_task(GlobalState *state);
uint8_t BAP_calculate_checksum(const char *sentence_body);
bap_command_t BAP_command_from_string(const char *cmd_str);
const char* BAP_command_to_string(bap_command_t cmd);
bap_parameter_t BAP_parameter_from_string(const char *param_str);
const char* BAP_parameter_to_string(bap_parameter_t param);

#endif /* BAP_H_ */
