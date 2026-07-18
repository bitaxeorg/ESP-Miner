#ifndef HTTP_AUTH_H
#define HTTP_AUTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// Maximum accepted lengths for the credentials.
#define HTTP_AUTH_USERNAME_MAX 32
#define HTTP_AUTH_PASSWORD_MAX 64

// Value for the "WWW-Authenticate" response header. Sending this on a 401 makes
// the browser show its native login dialog.
extern const char *const HTTP_AUTH_WWW_AUTHENTICATE;

// Load the stored credentials into the in-memory cache. Must be called once,
// after NVS has been initialised and before the HTTP server starts serving.
// Safe to call multiple times.
esp_err_t http_auth_init(void);

// True when a password has been configured (i.e. authentication is active).
bool http_auth_is_enabled(void);

// Copy the configured username (default "admin") into out. Returns ESP_OK.
esp_err_t http_auth_get_username(char *out, size_t out_len);

// Configure the credentials and persist them to NVS.
//   - A NULL or empty password DISABLES authentication (clears the password).
//   - username may be NULL/empty to keep the default ("admin").
// Returns ESP_OK on success or ESP_ERR_INVALID_ARG when the input is too long.
esp_err_t http_auth_set_credentials(const char *username, const char *password);

// Validate the request's "Authorization: Basic ..." header against the stored
// credentials. Returns ESP_OK when the credentials are valid, ESP_FAIL otherwise.
// Always returns ESP_FAIL when authentication is disabled.
esp_err_t http_auth_check_request(httpd_req_t *req);

// -------------------------------------------------------------------------
// Pure helpers (no NVS / no global state) - exposed for unit testing.
// -------------------------------------------------------------------------

// Parse a "Basic <base64(user:pass)>" header into user/pass. The scheme name is
// matched case-insensitively. The username stops at the first ':' so passwords
// may themselves contain ':'. Returns ESP_OK on success.
esp_err_t http_auth_parse_basic(const char *auth_header,
                                char *user_out, size_t user_len,
                                char *pass_out, size_t pass_len);

// Compute "<saltHex>:<hashHex>" (hex(salt) ':' hex(sha256(salt || password)))
// into out. Returns ESP_OK on success.
esp_err_t http_auth_compute_hash(const uint8_t *salt, size_t salt_len,
                                 const char *password,
                                 char *out, size_t out_len);

// Verify a plaintext password against a stored "<saltHex>:<hashHex>" string.
// Returns true when the password matches.
bool http_auth_verify_hash(const char *stored, const char *password);

// Constant-time string comparison. Returns true when the strings are equal.
bool http_auth_ct_equals(const char *a, const char *b);

#ifdef __cplusplus
}
#endif

#endif // HTTP_AUTH_H
