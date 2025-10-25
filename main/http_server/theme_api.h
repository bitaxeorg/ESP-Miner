#ifndef THEME_API_H
#define THEME_API_H

#include "esp_http_server.h"

#define DEFAULT_THEME "dark"
#define DEFAULT_COLORS "{ "\
        "--primary-color\":\"#F80421\", "\
        "--primary-color-text\":\"#ffffff\", "\
        "--highlight-bg\":\"#F80421\", "\
        "--highlight-text-color\":\"#ffffff\", "\
        "--focus-ring\":\"0 0 0 0.2rem rgba(248,4,33,0.2)\", "\
        "--slider-bg\":\"#dee2e6\", "\
        "--slider-range-bg\":\"#F80421\", "\
        "--slider-handle-bg\":\"#F80421\", "\
        "--progressbar-bg\":\"#dee2e6\", "\
        "--progressbar-value-bg\":\"#F80421\", "\
        "--checkbox-border\":\"#F80421\", "\
        "--checkbox-bg\":\"#F80421\", "\
        "--checkbox-hover-bg\":\"#df031d\", "\
        "--button-bg\":\"#F80421\", "\
        "--button-hover-bg\":\"#df031d\", "\
        "--button-focus-shadow\":\"0 0 0 2px #ffffff, 0 0 0 4px #F80421\", "\
        "--togglebutton-bg\":\"#F80421\", "\
        "--togglebutton-border\":\"1px solid #F80421\", "\
        "--togglebutton-hover-bg\":\"#df031d\", "\
        "--togglebutton-hover-border\":\"1px solid #df031d\", "\
        "--togglebutton-text-color\":\"#ffffff\" "\
        "}"

// Register theme API endpoints
esp_err_t register_theme_api_endpoints(httpd_handle_t server, void* ctx);

#endif // THEME_API_H
