#include "webhook_alert_utils.h"

#include <ctype.h>
#include <string.h>

static bool validate_port(const char *port_start, size_t port_len)
{
    if (port_len == 0 || port_len > 5) {
        return false;
    }

    uint32_t port = 0;
    for (size_t i = 0; i < port_len; i++) {
        if (!isdigit((unsigned char) port_start[i])) {
            return false;
        }
        port = port * 10 + (uint32_t) (port_start[i] - '0');
    }
    return port > 0 && port <= 65535;
}

static bool validate_hostname(const char *host, size_t host_len)
{
    if (host_len == 0 || host_len > 253 || host[0] == '.' || host[host_len - 1] == '.') {
        return false;
    }

    bool has_dot = false;
    size_t label_len = 0;
    for (size_t i = 0; i < host_len; i++) {
        unsigned char character = (unsigned char) host[i];
        if (character == '.') {
            if (label_len == 0 || label_len > 63 || host[i - 1] == '-') {
                return false;
            }
            has_dot = true;
            label_len = 0;
            continue;
        }
        if (!isalnum(character) && character != '-') {
            return false;
        }
        if (label_len == 0 && character == '-') {
            return false;
        }
        label_len++;
    }

    return has_dot && label_len > 0 && label_len <= 63 && host[host_len - 1] != '-';
}

bool WEBHOOK_ALERT_UTILS_is_valid_url(const char *url, size_t max_len)
{
    static const char scheme[] = "https://";
    if (url == NULL) {
        return false;
    }

    size_t len = strlen(url);
    if (len <= strlen(scheme) || len > max_len || strncmp(url, scheme, strlen(scheme)) != 0) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        unsigned char url_byte = (unsigned char) url[i];
        if (url_byte >= 0x80 || iscntrl(url_byte) || isspace(url_byte)) {
            return false;
        }
    }

    const char *authority = url + strlen(scheme);
    const char *authority_end = authority;
    while (*authority_end != '\0' && *authority_end != '/' && *authority_end != '?' &&
           *authority_end != '#') {
        authority_end++;
    }
    if (authority_end == authority || strchr(url, '#') != NULL) {
        return false;
    }

    size_t authority_len = (size_t) (authority_end - authority);
    if (memchr(authority, '@', authority_len) != NULL) {
        return false;
    }

    const char *host_end = memchr(authority, ':', authority_len);
    if (host_end != NULL) {
        size_t port_len = (size_t) (authority_end - host_end - 1);
        if (memchr(host_end + 1, ':', port_len) != NULL || !validate_port(host_end + 1, port_len)) {
            return false;
        }
    } else {
        host_end = authority_end;
    }

    return validate_hostname(authority, (size_t) (host_end - authority));
}

bool WEBHOOK_ALERT_UTILS_result_matches(uint32_t expected_request_id, uint32_t actual_request_id)
{
    return expected_request_id != 0 && expected_request_id == actual_request_id;
}

uint32_t WEBHOOK_ALERT_UTILS_remaining_ticks(uint32_t start_ticks, uint32_t now_ticks,
                                             uint32_t timeout_ticks)
{
    uint32_t elapsed_ticks = now_ticks - start_ticks;
    return elapsed_ticks >= timeout_ticks ? 0 : timeout_ticks - elapsed_ticks;
}

bool WEBHOOK_ALERT_UTILS_deadline_expired(int64_t start_us, int64_t now_us, uint32_t timeout_ms)
{
    if (now_us < start_us) {
        return false;
    }
    return now_us - start_us >= (int64_t) timeout_ms * 1000;
}
