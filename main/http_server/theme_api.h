#ifndef THEME_API_H
#define THEME_API_H

#include "esp_http_server.h"

#define DEFAULT_THEME "dark"
#define DEFAULT_COLOR "#F80421"

// Theme API endpoint handlers
esp_err_t theme_get_handler(httpd_req_t *req);
esp_err_t theme_post_handler(httpd_req_t *req);

#endif // THEME_API_H

