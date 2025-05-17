#include "theme_api.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "nvs_config.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "theme_api";
uiTheme_t currentTheme;

// Helper function to convert theme preset to string
static const char* themePresetToString(themePreset_t preset) {
    switch (preset) {
        case THEME_BITAXE_RED:
            return "THEME_BITAXE_RED";
        case THEME_BLOCKSTREAM_JADE:
            return "THEME_BLOCKSTREAM_JADE";
        case THEME_BLOCKSTREAM_BLUE:
            return "THEME_BLOCKSTREAM_BLUE";
        case THEME_SOLO_SATOSHI:
            return "THEME_SOLO_SATOSHI";
        case THEME_SOLO_MINING_CO:
            return "THEME_SOLO_MINING_CO";
        case THEME_BTCMAGAZINE:
            return "THEME_BTCMAGAZINE";
        case THEME_VOSKCOIN:
            return "THEME_VOSKCOIN";
        default:
            return "THEME_ACS_DEFAULT";
    }
}


uiTheme_t* getCurrnetTheme(void) {
    return &currentTheme;
}

themePreset_t getCurrentThemePreset(void) {
    return currentTheme.themePreset;
}

void initializeTheme(themePreset_t preset) {
    switch (preset) {
        case THEME_BITAXE_RED:
            strncpy(currentTheme.primaryColor, "#F80421", sizeof(currentTheme.primaryColor) - 1);
            currentTheme.primaryColor[sizeof(currentTheme.primaryColor) - 1] = '\0';
            
            strncpy(currentTheme.secondaryColor, "#FC4D62", sizeof(currentTheme.secondaryColor) - 1);
            currentTheme.secondaryColor[sizeof(currentTheme.secondaryColor) - 1] = '\0';
            
            strncpy(currentTheme.backgroundColor, "#070D17", sizeof(currentTheme.backgroundColor) - 1);
            currentTheme.backgroundColor[sizeof(currentTheme.backgroundColor) - 1] = '\0';
            
            strncpy(currentTheme.textColor, "#F80421", sizeof(currentTheme.textColor) - 1);
            currentTheme.textColor[sizeof(currentTheme.textColor) - 1] = '\0';
            
            strncpy(currentTheme.borderColor, "#FC4D62", sizeof(currentTheme.borderColor) - 1);
            currentTheme.borderColor[sizeof(currentTheme.borderColor) - 1] = '\0';
            
            currentTheme.themePreset = THEME_BITAXE_RED;
            break;
        case THEME_BLOCKSTREAM_JADE:
            strncpy(currentTheme.primaryColor, "#00B093", sizeof(currentTheme.primaryColor) - 1);
            currentTheme.primaryColor[sizeof(currentTheme.primaryColor) - 1] = '\0';
            
            strncpy(currentTheme.secondaryColor, "#006D62", sizeof(currentTheme.secondaryColor) - 1);
            currentTheme.secondaryColor[sizeof(currentTheme.secondaryColor) - 1] = '\0';
            
            strncpy(currentTheme.backgroundColor, "#111316", sizeof(currentTheme.backgroundColor) - 1);
            currentTheme.backgroundColor[sizeof(currentTheme.backgroundColor) - 1] = '\0';
            
            strncpy(currentTheme.textColor, "#21CCAB", sizeof(currentTheme.textColor) - 1);
            currentTheme.textColor[sizeof(currentTheme.textColor) - 1] = '\0';
            
            strncpy(currentTheme.borderColor, "#01544A", sizeof(currentTheme.borderColor) - 1);
            currentTheme.borderColor[sizeof(currentTheme.borderColor) - 1] = '\0';
            
            currentTheme.themePreset = THEME_BLOCKSTREAM_JADE;
            break;
        case THEME_BLOCKSTREAM_BLUE:
            strncpy(currentTheme.primaryColor, "#00C3FF", sizeof(currentTheme.primaryColor) - 1);
            currentTheme.primaryColor[sizeof(currentTheme.primaryColor) - 1] = '\0';
            
            strncpy(currentTheme.secondaryColor, "#00C3FF", sizeof(currentTheme.secondaryColor) - 1);
            currentTheme.secondaryColor[sizeof(currentTheme.secondaryColor) - 1] = '\0';
            
            strncpy(currentTheme.backgroundColor, "#111316", sizeof(currentTheme.backgroundColor) - 1);
            currentTheme.backgroundColor[sizeof(currentTheme.backgroundColor) - 1] = '\0';
            
            strncpy(currentTheme.textColor, "#00C3FF", sizeof(currentTheme.textColor) - 1);
            currentTheme.textColor[sizeof(currentTheme.textColor) - 1] = '\0';
            
            strncpy(currentTheme.borderColor, "#00C3FF", sizeof(currentTheme.borderColor) - 1);
            currentTheme.borderColor[sizeof(currentTheme.borderColor) - 1] = '\0';
            
            currentTheme.themePreset = THEME_BLOCKSTREAM_BLUE;
            break;
        case THEME_SOLO_SATOSHI:
            strncpy(currentTheme.primaryColor, "#F80421", sizeof(currentTheme.primaryColor) - 1);
            currentTheme.primaryColor[sizeof(currentTheme.primaryColor) - 1] = '\0';
            
            strncpy(currentTheme.secondaryColor, "#F7931A", sizeof(currentTheme.secondaryColor) - 1);
            currentTheme.secondaryColor[sizeof(currentTheme.secondaryColor) - 1] = '\0';
            
            strncpy(currentTheme.backgroundColor, "#070D17", sizeof(currentTheme.backgroundColor) - 1);
            currentTheme.backgroundColor[sizeof(currentTheme.backgroundColor) - 1] = '\0';
            
            strncpy(currentTheme.textColor, "#FFFFFF", sizeof(currentTheme.textColor) - 1);
            currentTheme.textColor[sizeof(currentTheme.textColor) - 1] = '\0';
            
            strncpy(currentTheme.borderColor, "#F7931A", sizeof(currentTheme.borderColor) - 1);
            currentTheme.borderColor[sizeof(currentTheme.borderColor) - 1] = '\0';
            
            currentTheme.themePreset = THEME_SOLO_SATOSHI;
            break;
        case THEME_SOLO_MINING_CO:
            strncpy(currentTheme.primaryColor, "#F15900", sizeof(currentTheme.primaryColor) - 1);
            currentTheme.primaryColor[sizeof(currentTheme.primaryColor) - 1] = '\0';
            
            strncpy(currentTheme.secondaryColor, "#C5900F", sizeof(currentTheme.secondaryColor) - 1);
            currentTheme.secondaryColor[sizeof(currentTheme.secondaryColor) - 1] = '\0';
            
            strncpy(currentTheme.backgroundColor, "#111316", sizeof(currentTheme.backgroundColor) - 1);
            currentTheme.backgroundColor[sizeof(currentTheme.backgroundColor) - 1] = '\0';
            
            strncpy(currentTheme.textColor, "#FFFFFF", sizeof(currentTheme.textColor) - 1);
            currentTheme.textColor[sizeof(currentTheme.textColor) - 1] = '\0';
            
            strncpy(currentTheme.borderColor, "#C5900F", sizeof(currentTheme.borderColor) - 1);
            currentTheme.borderColor[sizeof(currentTheme.borderColor) - 1] = '\0';
            
            currentTheme.themePreset = THEME_SOLO_MINING_CO;
            break;
        case THEME_BTCMAGAZINE:
            strncpy(currentTheme.primaryColor, "#FF9500", sizeof(currentTheme.primaryColor) - 1);
            currentTheme.primaryColor[sizeof(currentTheme.primaryColor) - 1] = '\0';
            
            strncpy(currentTheme.secondaryColor, "#FF9500", sizeof(currentTheme.secondaryColor) - 1);
            currentTheme.secondaryColor[sizeof(currentTheme.secondaryColor) - 1] = '\0';
            
            strncpy(currentTheme.backgroundColor, "#111316", sizeof(currentTheme.backgroundColor) - 1);
            currentTheme.backgroundColor[sizeof(currentTheme.backgroundColor) - 1] = '\0';
            
            strncpy(currentTheme.textColor, "#FFFFFF", sizeof(currentTheme.textColor) - 1);
            currentTheme.textColor[sizeof(currentTheme.textColor) - 1] = '\0';
            
            strncpy(currentTheme.borderColor, "#FF9500", sizeof(currentTheme.borderColor) - 1);
            currentTheme.borderColor[sizeof(currentTheme.borderColor) - 1] = '\0';
            
            currentTheme.themePreset = THEME_BTCMAGAZINE;
            break;
        case THEME_VOSKCOIN:
            strncpy(currentTheme.primaryColor, "#23B852", sizeof(currentTheme.primaryColor) - 1);
            currentTheme.primaryColor[sizeof(currentTheme.primaryColor) - 1] = '\0';
            
            strncpy(currentTheme.secondaryColor, "#23B852", sizeof(currentTheme.secondaryColor) - 1);
            currentTheme.secondaryColor[sizeof(currentTheme.secondaryColor) - 1] = '\0';
            
            strncpy(currentTheme.backgroundColor, "#111316", sizeof(currentTheme.backgroundColor) - 1);
            currentTheme.backgroundColor[sizeof(currentTheme.backgroundColor) - 1] = '\0';
            
            strncpy(currentTheme.textColor, "#FFFFFF", sizeof(currentTheme.textColor) - 1);
            currentTheme.textColor[sizeof(currentTheme.textColor) - 1] = '\0';
            
            strncpy(currentTheme.borderColor, "#23B852", sizeof(currentTheme.borderColor) - 1);
            currentTheme.borderColor[sizeof(currentTheme.borderColor) - 1] = '\0';
            
            currentTheme.themePreset = THEME_VOSKCOIN;
            break;
        default:
            strncpy(currentTheme.primaryColor, "#A7F3D0", sizeof(currentTheme.primaryColor) - 1);
            currentTheme.primaryColor[sizeof(currentTheme.primaryColor) - 1] = '\0';
            
            strncpy(currentTheme.secondaryColor, "#A7F3D0", sizeof(currentTheme.secondaryColor) - 1);
            currentTheme.secondaryColor[sizeof(currentTheme.secondaryColor) - 1] = '\0';
            
            strncpy(currentTheme.backgroundColor, "#161F1B", sizeof(currentTheme.backgroundColor) - 1);
            currentTheme.backgroundColor[sizeof(currentTheme.backgroundColor) - 1] = '\0';
            
            strncpy(currentTheme.textColor, "#A7F3D0", sizeof(currentTheme.textColor) - 1);
            currentTheme.textColor[sizeof(currentTheme.textColor) - 1] = '\0';
            
            strncpy(currentTheme.borderColor, "#A7F3D0", sizeof(currentTheme.borderColor) - 1);
            currentTheme.borderColor[sizeof(currentTheme.borderColor) - 1] = '\0';
            
            currentTheme.themePreset = THEME_ACS_DEFAULT;
            break;
    }
}

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

    initializeTheme(THEME_BITAXE_RED);
    uiTheme_t* theme = getCurrnetTheme();

    cJSON *root = cJSON_CreateObject();
    
    // Add theme preset
    cJSON_AddStringToObject(root, "themeName", themePresetToString(theme->themePreset));
    
    // Add all color values
    cJSON_AddStringToObject(root, "primaryColor", theme->primaryColor);
    cJSON_AddStringToObject(root, "secondaryColor", theme->secondaryColor);
    cJSON_AddStringToObject(root, "backgroundColor", theme->backgroundColor);
    cJSON_AddStringToObject(root, "textColor", theme->textColor);
    cJSON_AddStringToObject(root, "borderColor", theme->borderColor);

    const char *response = cJSON_Print(root);
    httpd_resp_sendstr(req, response);

    free((char *)response);
    cJSON_Delete(root);

    return ESP_OK;
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
    if ((item = cJSON_GetObjectItem(root, "theme")) != NULL) {
        nvs_config_set_string(NVS_CONFIG_THEME_NAME, item->valuestring);
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

