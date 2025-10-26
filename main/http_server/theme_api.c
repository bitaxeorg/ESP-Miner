#include "theme_api.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "nvs_config.h"
#include "cJSON.h"
#include "http_server.h"

static int theme_prebuffer_len = 256;

// Helper function to set CORS headers
static esp_err_t set_cors_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    return ESP_OK;
}

// Handle OPTIONS requests for CORS
static esp_err_t theme_options_handler(httpd_req_t *req)
{
    set_cors_headers(req);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// GET /api/theme handler
static esp_err_t theme_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    set_cors_headers(req);

    char *scheme = nvs_config_get_string(NVS_CONFIG_THEME_SCHEME);
    char *colors = nvs_config_get_string(NVS_CONFIG_THEME_COLORS);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "colorScheme", scheme);
    
    // Parse stored colors JSON string
    cJSON *colors_json = cJSON_Parse(colors);
    if (colors_json) {
        cJSON_AddItemToObject(root, "accentColors", colors_json);
    }

    esp_err_t res = HTTP_send_json(req, root, &theme_prebuffer_len);

    free(scheme);
    free(colors);

    cJSON_Delete(root);

    return res;
}

// POST /api/theme handler
static esp_err_t theme_post_handler(httpd_req_t *req)
{
    set_cors_headers(req);

    // Read POST data
    char content[1024];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read request");
        return ESP_FAIL;
    }
    content[ret] = '\0';

    cJSON *root = cJSON_Parse(content);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    // Update theme settings
    cJSON *item;
    if ((item = cJSON_GetObjectItem(root, "colorScheme")) != NULL) {
        nvs_config_set_string(NVS_CONFIG_THEME_SCHEME, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "accentColors")) != NULL) {
        char *colors_str = cJSON_Print(item);
        nvs_config_set_string(NVS_CONFIG_THEME_COLORS, colors_str);
        free(colors_str);
    }

    cJSON_Delete(root);
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

esp_err_t register_theme_api_endpoints(httpd_handle_t server, void* ctx)
{
    httpd_uri_t theme_get = {
        .uri = "/api/theme",
        .method = HTTP_GET,
        .handler = theme_get_handler,
        .user_ctx = ctx
    };

    httpd_uri_t theme_post = {
        .uri = "/api/theme",
        .method = HTTP_POST,
        .handler = theme_post_handler,
        .user_ctx = ctx
    };

    httpd_uri_t theme_options = {
        .uri = "/api/theme",
        .method = HTTP_OPTIONS,
        .handler = theme_options_handler,
        .user_ctx = ctx
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &theme_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &theme_post));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &theme_options));

    return ESP_OK;
}
