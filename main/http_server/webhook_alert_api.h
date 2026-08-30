#ifndef WEBHOOK_ALERT_API_H_
#define WEBHOOK_ALERT_API_H_

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t register_webhook_alert_api(httpd_handle_t server);

#endif /* WEBHOOK_ALERT_API_H_ */
