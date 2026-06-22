#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"

#include "http_auth.h"
#include "nvs_config.h"
#include "global_state.h"
#include "utils.h"

static const char *TAG = "http_auth";

extern GlobalState * GLOBAL_STATE;

static bool is_auth_required(httpd_req_t *req)
{
    // 1. If no password is set, auth is disabled globally
    char *password = nvs_config_get_string(NVS_CONFIG_AXEOS_PASSWORD);
    if (!password || strlen(password) == 0) {
        if (password) free(password);
        return false;
    }
    free(password);

    // 2. AP Mode physically proves ownership and bypasses all auth (grandma-proof)
    if (GLOBAL_STATE->SYSTEM_MODULE.ap_enabled) {
        return false;
    }

    // 3. For OPTIONS requests (CORS preflight), bypass auth
    if (req->method == HTTP_OPTIONS) {
        return false;
    }

    // 4. For GET requests, check if read-only auth is optionally enabled
    if (req->method == HTTP_GET) {
        return nvs_config_get_bool(NVS_CONFIG_AUTH_READ_REQUIRED);
    }

    // 5. All other methods (POST, PATCH, PUT) require auth
    return true;
}

static esp_err_t check_creds(const char *encoded_creds)
{
    if (!encoded_creds || strlen(encoded_creds) == 0) {
        return ESP_FAIL;
    }

    unsigned char decoded[128] = {0};
    size_t decoded_len = 0;
    int b64_ret = mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decoded_len, (const unsigned char *)encoded_creds, strlen(encoded_creds));
    if (b64_ret != 0) {
        return ESP_FAIL;
    }
    decoded[decoded_len] = '\0';

    char *colon = strchr((char *)decoded, ':');
    if (!colon) {
        return ESP_FAIL;
    }
    char *password = colon + 1;

    unsigned char hash[32];
    mbedtls_sha256((const unsigned char *)password, strlen(password), hash, 0);
    char hashed_hex[65];
    bin2hex(hash, 32, hashed_hex, sizeof(hashed_hex));

    char *stored_hash = nvs_config_get_string(NVS_CONFIG_AXEOS_PASSWORD);
    esp_err_t auth_result = ESP_FAIL;
    if (stored_hash) {
        if (strcmp(hashed_hex, stored_hash) == 0) {
            auth_result = ESP_OK;
        }
        free(stored_hash);
    }
    return auth_result;
}

esp_err_t http_auth_validate(httpd_req_t *req)
{
    if (!is_auth_required(req)) {
        return ESP_OK;
    }

    char auth_header[128] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header)) == ESP_OK) {
        if (strncmp(auth_header, "Basic ", 6) == 0) {
            if (check_creds(auth_header + 6) == ESP_OK) {
                return ESP_OK;
            }
        }
    }

    // Set WWW-Authenticate header
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Bitaxe\"");
    
    // Set response header to return json indicating auth is required
    httpd_resp_set_type(req, "application/json");
    
    // Allow CORS for 401 response so browser client can read response headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    const char *resp = "{\"authRequired\":1}";
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_send(req, resp, strlen(resp));
    
    return ESP_FAIL;
}

esp_err_t http_auth_websocket_validate(httpd_req_t *req)
{
    if (!is_auth_required(req)) {
        return ESP_OK;
    }

    size_t query_len = httpd_req_get_url_query_len(req) + 1;
    char *query_buf = NULL;
    bool auth_ok = false;
    if (query_len > 1) {
        query_buf = malloc(query_len);
        if (query_buf && httpd_req_get_url_query_str(req, query_buf, query_len) == ESP_OK) {
            char auth_val[128] = {0};
            if (httpd_query_key_value(query_buf, "auth", auth_val, sizeof(auth_val)) == ESP_OK) {
                if (check_creds(auth_val) == ESP_OK) {
                    auth_ok = true;
                }
            }
        }
        if (query_buf) {
            free(query_buf);
        }
    }

    if (!auth_ok) {
        ESP_LOGW(TAG, "WebSocket connection rejected: unauthorized");
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    return ESP_OK;
}

