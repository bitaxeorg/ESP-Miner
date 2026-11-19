#include <stdint.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "mbedtls/base64.h"

#include "http_auth.h"
#include "nvs_config.h"
#include "global_state.h"
#include "utils.h"

static const char *TAG = "http_auth";

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
    GlobalState *GLOBAL_STATE = (GlobalState *)httpd_get_global_user_ctx(req->handle);
    if (GLOBAL_STATE && GLOBAL_STATE->SYSTEM_MODULE.ap_enabled) {
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

    // 5. All other methods (POST, PATCH, PUT, DELETE) require auth
    return true;
}

static bool ct_hash_equals(const char *a, const char *b)
{
    if (strlen(a) != 64 || strlen(b) != 64) {
        return false;
    }
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < 64; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}

// TODO(security): Passwords are stored as unsalted SHA-256 in NVS and passed via HTTP Basic / WS query param on LAN HTTP.
static esp_err_t check_creds(const char *encoded_creds)
{
    if (!encoded_creds || strlen(encoded_creds) == 0) {
        return ESP_FAIL;
    }

    char *stored_hash = nvs_config_get_string(NVS_CONFIG_AXEOS_PASSWORD);
    if (!stored_hash || strlen(stored_hash) == 0) {
        free(stored_hash);
        return ESP_OK; // No password set
    }

    unsigned char decoded[256] = {0};
    size_t decoded_len = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decoded_len,
                              (const unsigned char *)encoded_creds, strlen(encoded_creds)) != 0) {
        free(stored_hash);
        return ESP_FAIL;
    }
    decoded[decoded_len] = '\0';

    char *colon = strchr((char *)decoded, ':');
    const char *password = colon ? (colon + 1) : (const char *)decoded;

    unsigned char hash[32];
    sha256_bin((const uint8_t *)password, strlen(password), hash);
    char hashed_hex[65];
    bin2hex(hash, 32, hashed_hex, sizeof(hashed_hex));

    esp_err_t auth_result = ct_hash_equals(hashed_hex, stored_hash) ? ESP_OK : ESP_FAIL;

    free(stored_hash);
    return auth_result;
}

esp_err_t http_auth_validate(httpd_req_t *req)
{
    if (!is_auth_required(req)) {
        return ESP_OK;
    }

    char auth_header[256] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header)) == ESP_OK) {
        if (strncmp(auth_header, "Basic ", 6) == 0) {
            if (check_creds(auth_header + 6) == ESP_OK) {
                return ESP_OK;
            }
        }
    }

    // Omit WWW-Authenticate for XHR requests from Axe-OS so the browser's native
    // Basic Auth popup does not conflict with the Web UI's login modal.
    char requested_with[32] = {0};
    bool is_xhr = (httpd_req_get_hdr_value_str(req, "X-Requested-With", requested_with, sizeof(requested_with)) == ESP_OK) &&
                  (strcasecmp(requested_with, "XMLHttpRequest") == 0);
    if (!is_xhr) {
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Restricted\"");
    }

    httpd_resp_set_type(req, "text/plain");

    const char *resp = "Unauthorized";
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_send(req, resp, strlen(resp));

    return ESP_FAIL;
}

esp_err_t http_auth_websocket_validate(httpd_req_t *req)
{
    if (!is_auth_required(req)) {
        return ESP_OK;
    }

    // 1. Check HTTP Authorization header (automatically sent by browsers after Basic Auth)
    char auth_header[256] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header)) == ESP_OK) {
        if (strncmp(auth_header, "Basic ", 6) == 0) {
            if (check_creds(auth_header + 6) == ESP_OK) {
                return ESP_OK;
            }
        }
    }

    // 2. Check "auth" URL query parameter (supports raw Base64 or percent-encoded Base64)
    size_t query_len = httpd_req_get_url_query_len(req) + 1;
    if (query_len > 1) {
        char *query_buf = malloc(query_len);
        if (query_buf) {
            if (httpd_req_get_url_query_str(req, query_buf, query_len) == ESP_OK) {
                char auth_val_enc[256] = {0};
                if (httpd_query_key_value(query_buf, "auth", auth_val_enc, sizeof(auth_val_enc)) == ESP_OK) {
                    if (check_creds(auth_val_enc) == ESP_OK) {
                        free(query_buf);
                        return ESP_OK;
                    }
                    if (strchr(auth_val_enc, '%') != NULL) {
                        char auth_val[256] = {0};
                        url_decode(auth_val, auth_val_enc);
                        if (check_creds(auth_val) == ESP_OK) {
                            free(query_buf);
                            return ESP_OK;
                        }
                    }
                }
            }
            free(query_buf);
        }
    }

    ESP_LOGW(TAG, "WebSocket connection rejected: unauthorized");
    return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
}

