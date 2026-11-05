#ifndef WEBHOOK_ALERTS_H_
#define WEBHOOK_ALERTS_H_

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef struct GlobalState GlobalState;

#define WEBHOOK_ALERT_URL_MAX_LEN 512
#define WEBHOOK_ALERT_SECRET_SENTINEL "********"

typedef enum {
    WEBHOOK_ALERT_TEST_GENERIC,
    WEBHOOK_ALERT_TEST_WATCHDOG,
    WEBHOOK_ALERT_TEST_BLOCK_FOUND,
    WEBHOOK_ALERT_TEST_BEST_DIFFICULTY,
} WebhookAlertTestEvent;

esp_err_t WEBHOOK_ALERTS_init(GlobalState *global_state);
void WEBHOOK_ALERTS_notify_startup(void);
void WEBHOOK_ALERTS_notify_block_found(double difficulty, double network_difficulty);
void WEBHOOK_ALERTS_notify_best_difficulty(double difficulty, double network_difficulty);
esp_err_t WEBHOOK_ALERTS_send_test(TickType_t timeout_ticks);
esp_err_t WEBHOOK_ALERTS_send_test_event(WebhookAlertTestEvent test_event, TickType_t timeout_ticks);

bool WEBHOOK_ALERTS_has_webhook(void);
bool WEBHOOK_ALERTS_is_valid_url(const char *url);
bool WEBHOOK_ALERTS_is_test_event_enabled(WebhookAlertTestEvent test_event);

#endif /* WEBHOOK_ALERTS_H_ */
