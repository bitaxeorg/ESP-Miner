#ifndef WEBHOOK_ALERT_API_H_
#define WEBHOOK_ALERT_API_H_

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t GET_webhook_alert_settings(httpd_req_t *req);
esp_err_t PATCH_webhook_alert_settings(httpd_req_t *req);
esp_err_t POST_webhook_alert_test(httpd_req_t *req);

#endif /* WEBHOOK_ALERT_API_H_ */
