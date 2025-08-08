/**
 * @file bap_subscription.h
 * @brief BAP subscription management
 * 
 * Handles parameter subscriptions, periodic updates, and subscription timeouts.
 */

#ifndef BAP_SUBSCRIPTION_H_
#define BAP_SUBSCRIPTION_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "global_state.h"
#include "bap_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool active;
    uint32_t last_response;         // When last subscription message was sent
    uint32_t update_interval_ms;
    uint32_t last_subscribe;        // When subscription was last renewed
} bap_subscription_t;

/**
 * @brief Initialize subscription management
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t BAP_subscription_init(void);

/**
 * @brief Handle subscription request
 * @param parameter Parameter name to subscribe to
 * @param value Optional interval value
 */
void BAP_subscription_handle_subscribe(const char *parameter, const char *value);

/**
 * @brief Handle unsubscription request
 * @param parameter Parameter name to unsubscribe from
 * @param value Unused
 */
void BAP_subscription_handle_unsubscribe(const char *parameter, const char *value);

/**
 * @brief Send subscription updates for all active subscriptions
 * @param state Global state pointer
 */
void BAP_send_subscription_update(GlobalState *state);

/**
 * @brief Start mode management task
 * @param state Global state pointer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t BAP_start_mode_management_task(GlobalState *state);

/**
 * @brief Start subscription update task
 * @param state Global state pointer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t BAP_start_subscription_task(GlobalState *state);

#ifdef __cplusplus
}
#endif

#endif /* BAP_SUBSCRIPTION_H_ */