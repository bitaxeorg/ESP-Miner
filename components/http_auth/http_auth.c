#include "http_auth.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "http_auth";

// Credentials live in their own NVS namespace so they can never collide with,
// or be dumped alongside, the regular device settings ("main" namespace).
#define AUTH_NVS_NAMESPACE "authcfg"
#define AUTH_NVS_KEY_USER  "user"
#define AUTH_NVS_KEY_HASH  "hash"

#define AUTH_SALT_LEN      16
// "<32 hex salt>:<64 hex sha256>" = 97 chars + NUL. Round up for headroom.
#define AUTH_HASH_STR_MAX  128
#define AUTH_DEFAULT_USER  "admin"

const char *const HTTP_AUTH_WWW_AUTHENTICATE = "Basic realm=\"AxeOS\", charset=\"UTF-8\"";

// In-memory cache of the stored credentials. The HTTP hot path reads this under
// the mutex and never touches NVS, keeping per-request cost negligible.
static struct {
    char username[HTTP_AUTH_USERNAME_MAX + 1];
    char hash[AUTH_HASH_STR_MAX]; // empty string == authentication disabled
    bool loaded;
} s_auth = {0};

static SemaphoreHandle_t s_mutex = NULL;

// ---------------------------------------------------------------------------
// Small pure helpers
// ---------------------------------------------------------------------------

static void bytes_to_hex(const uint8_t *in, size_t in_len, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < in_len; i++) {
        out[i * 2]     = digits[(in[i] >> 4) & 0x0F];
        out[i * 2 + 1] = digits[in[i] & 0x0F];
    }
    out[in_len * 2] = '\0';
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Decode a hex string into bytes. Returns the number of bytes written, or -1 on
// error (odd length or non-hex characters, or output buffer too small).
static int hex_to_bytes(const char *hex, size_t hex_len, uint8_t *out, size_t out_max)
{
    if (hex_len % 2 != 0 || hex_len / 2 > out_max) {
        return -1;
    }
    for (size_t i = 0; i < hex_len; i += 2) {
        int hi = hex_nibble(hex[i]);
        int lo = hex_nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        out[i / 2] = (uint8_t)((hi << 4) | lo);
    }
    return (int)(hex_len / 2);
}

bool http_auth_ct_equals(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    size_t la = strlen(a);
    size_t lb = strlen(b);
    // Compare lengths and every byte without early exit to avoid leaking timing.
    unsigned char diff = (unsigned char)(la ^ lb);
    size_t max = la > lb ? la : lb;
    for (size_t i = 0; i < max; i++) {
        char ca = i < la ? a[i] : 0;
        char cb = i < lb ? b[i] : 0;
        diff |= (unsigned char)(ca ^ cb);
    }
    return diff == 0;
}

esp_err_t http_auth_compute_hash(const uint8_t *salt, size_t salt_len,
                                 const char *password,
                                 char *out, size_t out_len)
{
    if (salt == NULL || password == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // salt hex + ':' + 64 hex + NUL
    if (out_len < salt_len * 2 + 1 + 64 + 1) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t digest[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    int rc = mbedtls_sha256_starts(&ctx, 0); // 0 => SHA-256
    if (rc == 0) rc = mbedtls_sha256_update(&ctx, salt, salt_len);
    if (rc == 0) rc = mbedtls_sha256_update(&ctx, (const unsigned char *)password, strlen(password));
    if (rc == 0) rc = mbedtls_sha256_finish(&ctx, digest);
    mbedtls_sha256_free(&ctx);
    if (rc != 0) {
        return ESP_FAIL;
    }

    bytes_to_hex(salt, salt_len, out);
    size_t pos = salt_len * 2;
    out[pos++] = ':';
    bytes_to_hex(digest, sizeof(digest), out + pos);
    return ESP_OK;
}

bool http_auth_verify_hash(const char *stored, const char *password)
{
    if (stored == NULL || password == NULL) {
        return false;
    }
    const char *colon = strchr(stored, ':');
    if (colon == NULL || colon == stored) {
        return false;
    }
    size_t salt_hex_len = (size_t)(colon - stored);

    uint8_t salt[AUTH_SALT_LEN];
    int salt_len = hex_to_bytes(stored, salt_hex_len, salt, sizeof(salt));
    if (salt_len <= 0) {
        return false;
    }

    char computed[AUTH_HASH_STR_MAX];
    if (http_auth_compute_hash(salt, (size_t)salt_len, password, computed, sizeof(computed)) != ESP_OK) {
        return false;
    }
    bool ok = http_auth_ct_equals(computed, stored);
    // Wipe the freshly computed hash from the stack.
    memset(computed, 0, sizeof(computed));
    return ok;
}

esp_err_t http_auth_parse_basic(const char *auth_header,
                                char *user_out, size_t user_len,
                                char *pass_out, size_t pass_len)
{
    if (auth_header == NULL || user_out == NULL || pass_out == NULL ||
        user_len == 0 || pass_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Expect: "Basic <token>" (scheme is case-insensitive per RFC 7617).
    const char *p = auth_header;
    if (strncasecmp(p, "Basic", 5) != 0) {
        return ESP_FAIL;
    }
    p += 5;
    if (*p != ' ' && *p != '\t') {
        return ESP_FAIL;
    }
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '\0') {
        return ESP_FAIL;
    }

    // Decode base64(user:pass).
    unsigned char decoded[HTTP_AUTH_USERNAME_MAX + HTTP_AUTH_PASSWORD_MAX + 4];
    size_t olen = 0;
    int rc = mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &olen,
                                   (const unsigned char *)p, strlen(p));
    if (rc != 0) {
        return ESP_FAIL;
    }
    decoded[olen] = '\0';

    // Split at the FIRST ':' (usernames cannot contain ':', passwords can).
    char *sep = strchr((char *)decoded, ':');
    if (sep == NULL) {
        memset(decoded, 0, sizeof(decoded));
        return ESP_FAIL;
    }
    *sep = '\0';
    const char *user = (const char *)decoded;
    const char *pass = sep + 1;

    esp_err_t ret = ESP_OK;
    if (strlen(user) >= user_len || strlen(pass) >= pass_len) {
        ret = ESP_ERR_INVALID_SIZE;
    } else {
        strlcpy(user_out, user, user_len);
        strlcpy(pass_out, pass, pass_len);
    }

    memset(decoded, 0, sizeof(decoded));
    return ret;
}

// ---------------------------------------------------------------------------
// NVS-backed credential storage (with an in-RAM cache for the hot path)
// ---------------------------------------------------------------------------

static esp_err_t ensure_mutex(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

// Read a string value from the auth namespace into buf. On any error buf is set
// to the empty string. Returns ESP_OK when a value was read, ESP_ERR_NOT_FOUND
// otherwise.
static esp_err_t nvs_read_str(const char *key, char *buf, size_t buf_len)
{
    buf[0] = '\0';
    nvs_handle_t h;
    esp_err_t err = nvs_open(AUTH_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    size_t len = buf_len;
    err = nvs_get_str(h, key, buf, &len);
    nvs_close(h);
    if (err != ESP_OK) {
        buf[0] = '\0';
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t http_auth_init(void)
{
    // Best-effort mutex creation. We deliberately continue even if it fails so
    // that a configured password is still reflected by http_auth_is_enabled():
    // that makes a mutex failure fail CLOSED (deny, recover via AP mode) rather
    // than fail open. There is no concurrency during init.
    ensure_mutex();

    char user[HTTP_AUTH_USERNAME_MAX + 1];
    char hash[AUTH_HASH_STR_MAX];
    if (nvs_read_str(AUTH_NVS_KEY_USER, user, sizeof(user)) != ESP_OK || user[0] == '\0') {
        strlcpy(user, AUTH_DEFAULT_USER, sizeof(user));
    }
    nvs_read_str(AUTH_NVS_KEY_HASH, hash, sizeof(hash));

    if (s_mutex != NULL) xSemaphoreTake(s_mutex, portMAX_DELAY);
    strlcpy(s_auth.username, user, sizeof(s_auth.username));
    strlcpy(s_auth.hash, hash, sizeof(s_auth.hash));
    s_auth.loaded = true;
    bool enabled = s_auth.hash[0] != '\0';
    if (s_mutex != NULL) xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Web authentication %s", enabled ? "ENABLED" : "disabled");
    return ESP_OK;
}

bool http_auth_is_enabled(void)
{
    // Reflect the stored password even when the mutex is unavailable so a
    // configured password is never silently ignored. When a password is set
    // but the mutex failed, http_auth_check_request() denies every request
    // (fail closed); recovery is still possible through AP setup mode.
    bool enabled;
    if (s_mutex != NULL) xSemaphoreTake(s_mutex, portMAX_DELAY);
    enabled = s_auth.loaded && s_auth.hash[0] != '\0';
    if (s_mutex != NULL) xSemaphoreGive(s_mutex);
    return enabled;
}

esp_err_t http_auth_get_username(char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_mutex == NULL) {
        strlcpy(out, AUTH_DEFAULT_USER, out_len);
        return ESP_OK;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    strlcpy(out, s_auth.username[0] ? s_auth.username : AUTH_DEFAULT_USER, out_len);
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t http_auth_set_credentials(const char *username, const char *password)
{
    esp_err_t err = ensure_mutex();
    if (err != ESP_OK) {
        return err;
    }

    // Validate lengths up-front.
    if (username != NULL && strlen(username) > HTTP_AUTH_USERNAME_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (password != NULL && strlen(password) > HTTP_AUTH_PASSWORD_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    char new_user[HTTP_AUTH_USERNAME_MAX + 1];
    if (username != NULL && username[0] != '\0') {
        strlcpy(new_user, username, sizeof(new_user));
    } else {
        // Keep the existing username, or fall back to the default.
        http_auth_get_username(new_user, sizeof(new_user));
    }

    char new_hash[AUTH_HASH_STR_MAX];
    new_hash[0] = '\0';
    bool disabling = (password == NULL || password[0] == '\0');
    if (!disabling) {
        uint8_t salt[AUTH_SALT_LEN];
        esp_fill_random(salt, sizeof(salt));
        err = http_auth_compute_hash(salt, sizeof(salt), password, new_hash, sizeof(new_hash));
        memset(salt, 0, sizeof(salt));
        if (err != ESP_OK) {
            return err;
        }
    }

    // Persist to NVS.
    nvs_handle_t h;
    err = nvs_open(AUTH_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }
    esp_err_t e1 = nvs_set_str(h, AUTH_NVS_KEY_USER, new_user);
    esp_err_t e2 = nvs_set_str(h, AUTH_NVS_KEY_HASH, new_hash);
    esp_err_t e3 = nvs_commit(h);
    nvs_close(h);
    if (e1 != ESP_OK || e2 != ESP_OK || e3 != ESP_OK) {
        ESP_LOGE(TAG, "Failed to persist credentials");
        return ESP_FAIL;
    }

    // Update the cache only after a successful commit.
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    strlcpy(s_auth.username, new_user, sizeof(s_auth.username));
    strlcpy(s_auth.hash, new_hash, sizeof(s_auth.hash));
    s_auth.loaded = true;
    xSemaphoreGive(s_mutex);

    memset(new_hash, 0, sizeof(new_hash));
    ESP_LOGI(TAG, "Web authentication %s", disabling ? "disabled" : "enabled/updated");
    return ESP_OK;
}

esp_err_t http_auth_check_request(httpd_req_t *req)
{
    if (req == NULL || s_mutex == NULL) {
        return ESP_FAIL;
    }

    size_t hdr_len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (hdr_len == 0 || hdr_len > 512) {
        return ESP_FAIL;
    }
    char *hdr = malloc(hdr_len + 1);
    if (hdr == NULL) {
        return ESP_FAIL;
    }
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, hdr_len + 1) != ESP_OK) {
        free(hdr);
        return ESP_FAIL;
    }

    char user[HTTP_AUTH_USERNAME_MAX + 1];
    char pass[HTTP_AUTH_PASSWORD_MAX + 1];
    esp_err_t parsed = http_auth_parse_basic(hdr, user, sizeof(user), pass, sizeof(pass));
    free(hdr);
    if (parsed != ESP_OK) {
        return ESP_FAIL;
    }

    // Snapshot the stored credentials under the mutex.
    char stored_user[HTTP_AUTH_USERNAME_MAX + 1];
    char stored_hash[AUTH_HASH_STR_MAX];
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    strlcpy(stored_user, s_auth.username, sizeof(stored_user));
    strlcpy(stored_hash, s_auth.hash, sizeof(stored_hash));
    xSemaphoreGive(s_mutex);

    esp_err_t result = ESP_FAIL;
    if (stored_hash[0] != '\0') {
        bool user_ok = http_auth_ct_equals(user, stored_user);
        bool pass_ok = http_auth_verify_hash(stored_hash, pass);
        if (user_ok && pass_ok) {
            result = ESP_OK;
        }
    }

    // Wipe sensitive stack buffers.
    memset(user, 0, sizeof(user));
    memset(pass, 0, sizeof(pass));
    memset(stored_hash, 0, sizeof(stored_hash));
    return result;
}
