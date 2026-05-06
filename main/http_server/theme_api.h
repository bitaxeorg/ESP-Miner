#ifndef THEME_API_H
#define THEME_API_H

#include "esp_http_server.h"

#define DEFAULT_THEME "dark"
#define DEFAULT_COLOR "#F80421"

// Register theme API endpoints
esp_err_t register_theme_api_endpoints(httpd_handle_t server, void* ctx);

#endif // THEME_API_H
