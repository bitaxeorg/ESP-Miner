#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <esp_http_server.h>
#include "esp_log.h"
#include "lwip/inet.h"

#include "http_cors.h"
#include "global_state.h"
#include "nvs_config.h"

static const char *TAG = "http_cors";

extern GlobalState * GLOBAL_STATE;

static esp_err_t ip_in_private_range(uint32_t address) {
    uint32_t ip_address = ntohl(address);

    // 10.0.0.0 - 10.255.255.255 (Class A)
    if ((ip_address >= 0x0A000000) && (ip_address <= 0x0AFFFFFF)) {
        return ESP_OK;
    }

    // 172.16.0.0 - 172.31.255.255 (Class B)
    if ((ip_address >= 0xAC100000) && (ip_address <= 0xAC1FFFFF)) {
        return ESP_OK;
    }

    // 192.168.0.0 - 192.168.255.255 (Class C)
    if ((ip_address >= 0xC0A80000) && (ip_address <= 0xC0A8FFFF)) {
        return ESP_OK;
    }

    return ESP_FAIL;
}

static uint32_t extract_origin_ip_addr(char *origin)
{
    char host_str[128];
    uint32_t origin_ip_addr = 0;

    // Find the start of the hostname in the Origin header
    const char *prefix = "http://";
    char *host_start = strstr(origin, prefix);
    if (host_start) {
        host_start += strlen(prefix); // Move past "http://"

        // Extract the hostname portion (up to the next '/')
        char *host_end = strchr(host_start, '/');
        size_t host_len = host_end ? (size_t)(host_end - host_start) : strlen(host_start);
        if (host_len < sizeof(host_str)) {
            strncpy(host_str, host_start, host_len);
            host_str[host_len] = '\0'; // Null-terminate the string

            // Check if it's an IP address or hostname
            struct in_addr addr;
            if (inet_pton(AF_INET, host_str, &addr) == 1) {
                origin_ip_addr = addr.s_addr;
                ESP_LOGD(TAG, "Extracted IP address %lu", origin_ip_addr);
            } else {
                ESP_LOGD(TAG, "Origin contains hostname: %s (not an IP)", host_str);
                // For hostnames, return 0 to indicate it's not an IP address
                origin_ip_addr = 0;
            }
        } else {
            ESP_LOGW(TAG, "Hostname string is too long: %s", host_start);
        }
    }

    return origin_ip_addr;
}

static esp_err_t http_cors_set_headers(httpd_req_t * req)
{
    esp_err_t err;

    err = httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    err = httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    err = httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

// Helper function to normalize hostname by stripping ".local" suffix if present
// This prevents Avahi from creating duplicate ".local.local" hostnames
void http_cors_normalize_hostname(char *hostname, size_t max_len) {
    if (hostname == NULL || strlen(hostname) == 0) {
        return;
    }

    size_t len = strlen(hostname);
    const char *suffix = ".local";
    size_t suffix_len = strlen(suffix);

    // Check if hostname ends with ".local" (case-insensitive)
    if (len > suffix_len &&
        strcasecmp(hostname + len - suffix_len, suffix) == 0) {
        // Strip the ".local" suffix
        hostname[len - suffix_len] = '\0';
        ESP_LOGD(TAG, "Normalized hostname from '%s.local' to '%s'", hostname, hostname);
    }
}

esp_err_t http_cors_check(httpd_req_t * req)
{
    if (GLOBAL_STATE->SYSTEM_MODULE.ap_enabled == true) {
        ESP_LOGI(TAG, "Device in AP mode. Allowing CORS.");
        return http_cors_set_headers(req);
    }

    int sockfd = httpd_req_to_sockfd(req);
    char ipstr[INET6_ADDRSTRLEN];
    struct sockaddr_in6 addr;   // esp_http_server uses IPv6 addressing
    socklen_t addr_size = sizeof(addr);

    if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_size) < 0) {
        ESP_LOGE(TAG, "Error getting client IP");
        return ESP_FAIL;
    }

    uint32_t request_ip_addr = addr.sin6_addr.un.u32_addr[3];

    // // Convert to IPv6 string
    // inet_ntop(AF_INET, &addr.sin6_addr, ipstr, sizeof(ipstr));

    // Convert to IPv4 string
    inet_ntop(AF_INET, &request_ip_addr, ipstr, sizeof(ipstr));

    // Attempt to get the Origin header.
    char origin[128];
    uint32_t origin_ip_addr;
    if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) == ESP_OK) {
        ESP_LOGD(TAG, "Origin header: %s", origin);
        origin_ip_addr = extract_origin_ip_addr(origin);
    } else {
        ESP_LOGD(TAG, "No origin header found.");
        origin_ip_addr = request_ip_addr;
    }

    if (origin_ip_addr != 0 && ip_in_private_range(origin_ip_addr) == ESP_OK && ip_in_private_range(request_ip_addr) == ESP_OK) {
        ESP_LOGD(TAG, "Origin and IP both in private range. Allowing.");
        return http_cors_set_headers(req);
    }
    
    // If origin contains hostname (origin_ip_addr == 0), proceed to hostname validation
    if (origin_ip_addr == 0) {
        ESP_LOGD(TAG, "Origin contains hostname, proceeding to hostname validation");
    }

    // Check if Origin header matches the avahi hostname or is a local-network hostname
    if (httpd_req_get_hdr_value_len(req, "Origin") > 0) {
        httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin));
        ESP_LOGD(TAG, "Origin header: %s", origin);

        // Extract the host portion from the origin for local-hostname validation
        char host_str[128] = {0};
        const char *prefix = "http://";
        char *host_start = strstr(origin, prefix);
        bool is_local_hostname = false;
        if (host_start) {
            host_start += strlen(prefix);
            // Strip port if present
            char *colon = strchr(host_start, ':');
            char *slash = strchr(host_start, '/');
            size_t host_len = 0;
            if (colon) {
                host_len = colon - host_start;
            } else if (slash) {
                host_len = slash - host_start;
            } else {
                host_len = strlen(host_start);
            }
            if (host_len > 0 && host_len < sizeof(host_str)) {
                strncpy(host_str, host_start, host_len);
                host_str[host_len] = '\0';

                // Allow any .local hostname (mDNS, inherently local network)
                size_t hlen = strlen(host_str);
                if (hlen > 6 && strcasecmp(host_str + hlen - 6, ".local") == 0) {
                    is_local_hostname = true;
                    ESP_LOGD(TAG, "Origin host '%s' is a .local mDNS hostname - allowing", host_str);
                }
                // Allow any bare hostname (no dots, only resolvable on local network)
                else if (strchr(host_str, '.') == NULL) {
                    is_local_hostname = true;
                    ESP_LOGD(TAG, "Origin host '%s' is a bare local hostname - allowing", host_str);
                }
            }
        }

        if (is_local_hostname) {
            ESP_LOGD(TAG, "Request from local hostname - allowing access");
            return http_cors_set_headers(req);
        }

        // Fall back to exact match against this device's configured hostname
        char *hostname = nvs_config_get_string(NVS_CONFIG_HOSTNAME);
        ESP_LOGD(TAG, "Configured hostname: %s", hostname);
        // Match origin as http://<hostname>.local[:port] or http://<hostname>[:port]
        const char *patterns[] = { "http://%s.local", "http://%s" };
        bool matched = false;
        for (int i = 0; i < 2 && !matched; i++) {
            char expected[256];
            snprintf(expected, sizeof(expected), patterns[i], hostname);
            size_t len = strlen(expected);
            // Origin must start with expected, followed by end-of-string or ':port'
            if (strncmp(origin, expected, len) == 0 &&
                (origin[len] == '\0' || origin[len] == ':')) {
                matched = true;
            }
        }
        free(hostname);
        if (matched) {
            ESP_LOGD(TAG, "Request from hostname - allowing access");
            return http_cors_set_headers(req);
        }
    } else {
        ESP_LOGD(TAG, "No Origin header found");
    }

    ESP_LOGI(TAG, "Client is NOT in the private ip ranges or same range as server.");
    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    return ESP_FAIL;
}

