#ifndef HTTP_AUTH_H_
#define HTTP_AUTH_H_

#include <esp_http_server.h>
#include <stdbool.h>

esp_err_t http_auth_validate(httpd_req_t *req);
esp_err_t http_auth_websocket_validate(httpd_req_t *req);

#endif /* HTTP_AUTH_H_ */
