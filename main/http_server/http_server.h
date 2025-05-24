#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_
#include <esp_http_server.h>
#include "esp_err.h"

esp_err_t start_rest_server(void *pvParameters);
esp_err_t is_network_allowed(httpd_req_t *req);
esp_err_t set_cors_headers(httpd_req_t *req);

#endif