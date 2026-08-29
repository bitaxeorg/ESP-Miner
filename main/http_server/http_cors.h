#ifndef HTTP_CORS_H_
#define HTTP_CORS_H_

#include <esp_http_server.h>

esp_err_t http_cors_check(httpd_req_t * req);
void http_cors_normalize_hostname(char *hostname, size_t max_len);

#endif /* HTTP_CORS_H_ */
