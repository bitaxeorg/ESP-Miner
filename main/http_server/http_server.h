#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

#include <esp_http_server.h>

#include "cJSON.h"

esp_err_t is_network_allowed(httpd_req_t * req);
esp_err_t set_cors_headers(httpd_req_t * req);
bool is_auth_required(httpd_req_t *req);
esp_err_t check_auth_value(const char *encoded_creds);
esp_err_t validate_authentication(httpd_req_t *req);
esp_err_t start_rest_server(void *pvParameters);
esp_err_t HTTP_send_json(httpd_req_t * req, const cJSON * item, int * prebuffer_len);

#endif /* HTTP_SERVER_H_ */
