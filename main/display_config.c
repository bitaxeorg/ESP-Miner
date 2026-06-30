#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "display_config.h"
#include "connect.h"
#include "nvs_config.h"

static inline bool append_string(char **dst, size_t *remaining, const char *str)
{
    if (!str) str = "--";
    size_t len = strlen(str);
    if (len > *remaining) return false;
    memcpy(*dst, str, len);
    *dst += len;
    *remaining -= len;
    return true;
}

static inline bool append_formatted(char **dst, size_t *remaining,
                                    char *temp_buffer, size_t temp_size,
                                    const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int len = vsnprintf(temp_buffer, temp_size, format, args);
    va_end(args);

    if (len <= 0 || (size_t)len >= temp_size) return false;
    return append_string(dst, remaining, temp_buffer);
}

typedef enum {
    VAR_TYPE_STRING_PTR,
    VAR_TYPE_STRING_ARRAY,
    VAR_TYPE_FLOAT,
    VAR_TYPE_INT32,
    VAR_TYPE_UINT16,
    VAR_TYPE_UINT32,
    VAR_TYPE_UINT64,
    VAR_TYPE_CUSTOM,
} var_type_t;

typedef void (*handler_func_t)(GlobalState *GLOBAL_STATE, char *temp, size_t temp_size, char **dst, size_t *remaining);

typedef struct {
    const char    *var_name;
    var_type_t     type;
    size_t         offset;
    const char    *format;
    handler_func_t custom_handler;
} var_entry_t;

// Custom handler functions
static void handle_efficiency(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    float power = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.power;
    float hashrate = GLOBAL_STATE->SYSTEM_MODULE.current_hashrate;
    float efficiency = (power > 0.0f && hashrate > 0.0f) ? power / (hashrate / 1000.0f) : 0.0f;
    append_formatted(dst, remaining, temp, ts, "%.2f", efficiency);
}

static void handle_pool_url(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    const char *pool = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback
        ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_url
        : GLOBAL_STATE->SYSTEM_MODULE.pool_url;
    append_string(dst, remaining, pool);
}

static void handle_rssi(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    int8_t rssi = -128;
    if (GLOBAL_STATE->SYSTEM_MODULE.is_connected) {
        get_wifi_current_rssi(&rssi);
    }
    append_formatted(dst, remaining, temp, ts, "%d", (int)rssi);
}

static void handle_signal(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    int8_t rssi = -128;
    if (GLOBAL_STATE->SYSTEM_MODULE.is_connected) {
        get_wifi_current_rssi(&rssi);
    }
    const char *quality = (rssi >= -50) ? "Excellent"
                        : (rssi >= -60) ? "Good"
                        : (rssi >= -70) ? "Fair"
                        : (rssi > -128) ? "Poor"
                        :                 "--";
    append_string(dst, remaining, quality);
}

static void handle_uptime(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    uint64_t uptime_us = esp_timer_get_time() - GLOBAL_STATE->SYSTEM_MODULE.start_time;
    uint32_t total_seconds = uptime_us / 1000000;

    unsigned long days    = total_seconds / 86400;
    total_seconds        %= 86400;
    unsigned long hours   = total_seconds / 3600;
    total_seconds        %= 3600;
    unsigned long minutes = total_seconds / 60;
    unsigned long seconds = total_seconds % 60;

    if (days > 0) {
        snprintf(temp, ts, "%lud %luh %lum %lus", days, hours, minutes, seconds);
    } else if (hours > 0) {
        snprintf(temp, ts, "%luh %lum %lus", hours, minutes, seconds);
    } else if (minutes > 0) {
        snprintf(temp, ts, "%lum %lus", minutes, seconds);
    } else {
        snprintf(temp, ts, "%lus", seconds);
    }
    append_string(dst, remaining, temp);
}

static void handle_target_temp(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    uint16_t target = nvs_config_get_u16(NVS_CONFIG_TEMP_TARGET);
    append_formatted(dst, remaining, temp, ts, "%u", target);
}

static void handle_block_found(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    const char *found = GLOBAL_STATE->SYSTEM_MODULE.block_found ? "Yes" : "No";
    append_string(dst, remaining, found);
}

static void handle_hostname(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    if (GLOBAL_STATE && strlen(GLOBAL_STATE->SYSTEM_MODULE.mdns_hostname) > 0) {
        append_string(dst, remaining, GLOBAL_STATE->SYSTEM_MODULE.mdns_hostname);
    } else {
        char *hostname = nvs_config_get_string(NVS_CONFIG_HOSTNAME);
        append_string(dst, remaining, hostname);
        free(hostname);
    }
}

static void handle_free_heap(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%u", esp_get_free_heap_size());
}

static void handle_is_using_fallback_stratum(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    const char *str = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? "Yes" : "No";
    append_string(dst, remaining, str);
}

// Variables configuration using offsetof for simple fields
static const var_entry_t variables[] = {
    { "hashrate",          VAR_TYPE_FLOAT,        offsetof(GlobalState, SYSTEM_MODULE.current_hashrate),            "%.2f", NULL },
    { "hashrate_1m",       VAR_TYPE_FLOAT,        offsetof(GlobalState, SYSTEM_MODULE.hashrate_1m),                 "%.2f", NULL },
    { "hashrate_10m",      VAR_TYPE_FLOAT,        offsetof(GlobalState, SYSTEM_MODULE.hashrate_10m),                "%.2f", NULL },
    { "hashrate_1h",       VAR_TYPE_FLOAT,        offsetof(GlobalState, SYSTEM_MODULE.hashrate_1h),                 "%.2f", NULL },
    { "hashrate_expected", VAR_TYPE_FLOAT,        offsetof(GlobalState, POWER_MANAGEMENT_MODULE.expected_hashrate), "%.1f", NULL },

    { "frequency",         VAR_TYPE_FLOAT,        offsetof(GlobalState, POWER_MANAGEMENT_MODULE.frequency_value),   "%.0f", NULL },
    { "power",             VAR_TYPE_FLOAT,        offsetof(GlobalState, POWER_MANAGEMENT_MODULE.power),             "%.1f", NULL },
    { "efficiency",        VAR_TYPE_CUSTOM,       0,                                                                NULL,   handle_efficiency },
    { "voltage",           VAR_TYPE_FLOAT,        offsetof(GlobalState, POWER_MANAGEMENT_MODULE.voltage),           "%.2f", NULL },
    { "core_voltage",      VAR_TYPE_FLOAT,        offsetof(GlobalState, POWER_MANAGEMENT_MODULE.core_voltage),      "%.2f", NULL },
    { "current",           VAR_TYPE_FLOAT,        offsetof(GlobalState, POWER_MANAGEMENT_MODULE.current),           "%.2f", NULL },
    { "power_fault",       VAR_TYPE_UINT16,       offsetof(GlobalState, SYSTEM_MODULE.power_fault),                 "%u",   NULL },

    { "asic1_temp",        VAR_TYPE_FLOAT,        offsetof(GlobalState, POWER_MANAGEMENT_MODULE.chip_temp_avg),     "%.1f", NULL },
    { "asic2_temp",        VAR_TYPE_FLOAT,        offsetof(GlobalState, POWER_MANAGEMENT_MODULE.chip_temp2_avg),    "%.1f", NULL },
    { "vr_temp",           VAR_TYPE_FLOAT,        offsetof(GlobalState, POWER_MANAGEMENT_MODULE.vr_temp),           "%.1f", NULL },
    { "target_temp",       VAR_TYPE_CUSTOM,       0,                                                                NULL,   handle_target_temp },

    { "fan_perc",          VAR_TYPE_FLOAT,        offsetof(GlobalState, POWER_MANAGEMENT_MODULE.fan_perc),          "%.1f", NULL },
    { "fan1_rpm",          VAR_TYPE_UINT16,       offsetof(GlobalState, POWER_MANAGEMENT_MODULE.fan_rpm),           "%u",   NULL },
    { "fan2_rpm",          VAR_TYPE_UINT16,       offsetof(GlobalState, POWER_MANAGEMENT_MODULE.fan2_rpm),          "%u",   NULL },

    { "pool_url",          VAR_TYPE_CUSTOM,       0,                                                                NULL,   handle_pool_url },
    { "pool_diff",         VAR_TYPE_UINT16,       offsetof(GlobalState, SYSTEM_MODULE.pool_difficulty),             "%u",   NULL },
    { "response_time",     VAR_TYPE_FLOAT,        offsetof(GlobalState, SYSTEM_MODULE.response_time),               "%.2f", NULL },
    { "pool_connection_info", VAR_TYPE_STRING_ARRAY, offsetof(GlobalState, SYSTEM_MODULE.pool_connection_info),     NULL,   NULL },
    { "is_using_fallback_stratum", VAR_TYPE_CUSTOM, 0,                                                              NULL,   handle_is_using_fallback_stratum },

    { "shares_accepted",   VAR_TYPE_UINT64,       offsetof(GlobalState, SYSTEM_MODULE.shares_accepted),             "%llu", NULL },
    { "shares_rejected",   VAR_TYPE_UINT64,       offsetof(GlobalState, SYSTEM_MODULE.shares_rejected),             "%llu", NULL },
    { "work_received",     VAR_TYPE_UINT64,       offsetof(GlobalState, SYSTEM_MODULE.work_received),               "%llu", NULL },
    { "error_percentage",  VAR_TYPE_FLOAT,        offsetof(GlobalState, SYSTEM_MODULE.error_percentage),            "%.2f", NULL },
    { "session_diff",      VAR_TYPE_STRING_ARRAY, offsetof(GlobalState, SYSTEM_MODULE.best_session_diff_string),    NULL,   NULL },
    { "best_diff",         VAR_TYPE_STRING_ARRAY, offsetof(GlobalState, SYSTEM_MODULE.best_diff_string),            NULL,   NULL },
    { "block_found",       VAR_TYPE_CUSTOM,       0,                                                                NULL,   handle_block_found },

    { "ssid",              VAR_TYPE_STRING_PTR,   offsetof(GlobalState, SYSTEM_MODULE.ssid),                        NULL,   NULL },
    { "wifi_status",       VAR_TYPE_STRING_ARRAY, offsetof(GlobalState, SYSTEM_MODULE.wifi_status),                 NULL,   NULL },
    { "ip",                VAR_TYPE_STRING_ARRAY, offsetof(GlobalState, SYSTEM_MODULE.ip_addr_str),                 NULL,   NULL },
    { "ipv6",              VAR_TYPE_STRING_ARRAY, offsetof(GlobalState, SYSTEM_MODULE.ipv6_addr_str),               NULL,   NULL },
    { "rssi",              VAR_TYPE_CUSTOM,       0,                                                                NULL,   handle_rssi },
    { "signal",            VAR_TYPE_CUSTOM,       0,                                                                NULL,   handle_signal },
    { "uptime",            VAR_TYPE_CUSTOM,       0,                                                                NULL,   handle_uptime },

    { "network_diff",      VAR_TYPE_STRING_ARRAY, offsetof(GlobalState, network_diff_string),                      NULL,   NULL },
    { "scriptsig",         VAR_TYPE_STRING_ARRAY, offsetof(GlobalState, scriptsig),                                 NULL,   NULL },
    { "block_height",      VAR_TYPE_INT32,        offsetof(GlobalState, block_height),                              "%d",   NULL },

    { "hostname",          VAR_TYPE_CUSTOM,       0,                                                                NULL,   handle_hostname },
    { "device_model",      VAR_TYPE_STRING_PTR,   offsetof(GlobalState, DEVICE_CONFIG.family.name),                 NULL,   NULL },
    { "asic_model",        VAR_TYPE_STRING_PTR,   offsetof(GlobalState, DEVICE_CONFIG.family.asic.name),            NULL,   NULL },
    { "board_version",     VAR_TYPE_STRING_PTR,   offsetof(GlobalState, DEVICE_CONFIG.board_version),               NULL,   NULL },

    { "version",           VAR_TYPE_STRING_PTR,   offsetof(GlobalState, SYSTEM_MODULE.version),                     NULL,   NULL },
    { "axe_os_version",    VAR_TYPE_STRING_PTR,   offsetof(GlobalState, SYSTEM_MODULE.axeOSVersion),                NULL,   NULL },
    { "free_heap",         VAR_TYPE_CUSTOM,       0,                                                                NULL,   handle_free_heap },
    { NULL,                0,                     0,                                                                NULL,   NULL }
};

static void resolve_variable(GlobalState *GLOBAL_STATE, const var_entry_t *e, char *temp_buffer, size_t temp_size, char **dst, size_t *remaining)
{
    if (e->type == VAR_TYPE_CUSTOM) {
        if (e->custom_handler) {
            e->custom_handler(GLOBAL_STATE, temp_buffer, temp_size, dst, remaining);
        }
        return;
    }

    const void *base = (const void *)GLOBAL_STATE;
    const void *ptr = (const char *)base + e->offset;

    switch (e->type) {
        case VAR_TYPE_STRING_PTR: {
            const char *str = *(const char **)ptr;
            append_string(dst, remaining, str);
            break;
        }
        case VAR_TYPE_STRING_ARRAY: {
            const char *str = (const char *)ptr;
            append_string(dst, remaining, str);
            break;
        }
        case VAR_TYPE_FLOAT: {
            float val = *(const float *)ptr;
            append_formatted(dst, remaining, temp_buffer, temp_size, e->format ? e->format : "%f", val);
            break;
        }
        case VAR_TYPE_INT32: {
            int32_t val = *(const int32_t *)ptr;
            append_formatted(dst, remaining, temp_buffer, temp_size, e->format ? e->format : "%d", val);
            break;
        }
        case VAR_TYPE_UINT16: {
            uint16_t val = *(const uint16_t *)ptr;
            append_formatted(dst, remaining, temp_buffer, temp_size, e->format ? e->format : "%u", val);
            break;
        }
        case VAR_TYPE_UINT32: {
            uint32_t val = *(const uint32_t *)ptr;
            append_formatted(dst, remaining, temp_buffer, temp_size, e->format ? e->format : "%u", val);
            break;
        }
        case VAR_TYPE_UINT64: {
            uint64_t val = *(const uint64_t *)ptr;
            append_formatted(dst, remaining, temp_buffer, temp_size, e->format ? e->format : "%llu", (unsigned long long)val);
            break;
        }
        default:
            break;
    }
}

esp_err_t display_config_format_string(GlobalState *GLOBAL_STATE,
                                const char *input,
                                char *output,
                                size_t max_len)
{
    if (!GLOBAL_STATE || !input || !output || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char temp_buffer[32];
    char var_buffer[32];
    const char *src = input;
    char *dst = output;
    size_t remaining = max_len - 1;

    while (*src && remaining > 0) {
        if (*src == '{') {
            const char *brace_end = strchr(src + 1, '}');
            if (brace_end) {
                size_t var_len = brace_end - src - 1;
                if (var_len > 0 && var_len < sizeof(var_buffer)) {
                    memcpy(var_buffer, src + 1, var_len);
                    var_buffer[var_len] = '\0';

                    // Parse optional padding width: {variable:width} or {variable:-width}
                    char *colon = strchr(var_buffer, ':');
                    int pad_width = 0;
                    bool pad_left = false; // default: pad left (right-aligned)
                    if (colon) {
                        *colon = '\0'; // Split variable name from formatting options
                        char *width_str = colon + 1;
                        if (width_str[0] == '-') {
                            pad_left = true;
                            width_str++;
                        }
                        pad_width = atoi(width_str);
                    }

                    bool found = false;
                    for (const var_entry_t *e = variables; e->var_name; ++e) {
                        if (strcmp(var_buffer, e->var_name) == 0) {
                            char temp_var_val[64];
                            size_t temp_remaining = sizeof(temp_var_val) - 1;
                            char *temp_dst = temp_var_val;

                            resolve_variable(GLOBAL_STATE, e, temp_buffer, sizeof(temp_buffer),
                                             &temp_dst, &temp_remaining);
                            *temp_dst = '\0';

                            size_t val_len = strlen(temp_var_val);
                            if (pad_width > 0 && val_len < pad_width) {
                                size_t spaces_to_add = pad_width - val_len;
                                if (spaces_to_add > remaining) {
                                    spaces_to_add = remaining;
                                }
                                if (pad_left) {
                                    // Left-aligned: append string first, then pad spaces
                                    if (val_len <= remaining) {
                                        memcpy(dst, temp_var_val, val_len);
                                        dst += val_len;
                                        remaining -= val_len;
                                    }
                                    memset(dst, ' ', spaces_to_add);
                                    dst += spaces_to_add;
                                    remaining -= spaces_to_add;
                                } else {
                                    // Right-aligned: pad spaces first, then append string
                                    memset(dst, ' ', spaces_to_add);
                                    dst += spaces_to_add;
                                    remaining -= spaces_to_add;
                                    if (val_len <= remaining) {
                                        memcpy(dst, temp_var_val, val_len);
                                        dst += val_len;
                                        remaining -= val_len;
                                    }
                                }
                            } else {
                                // No padding or string is already longer than pad_width
                                if (val_len <= remaining) {
                                    memcpy(dst, temp_var_val, val_len);
                                    dst += val_len;
                                    remaining -= val_len;
                                }
                            }

                            src = brace_end + 1;
                            found = true;
                            break;
                        }
                    }
                    if (found) continue;
                }
            }
        }
        *dst++ = *src++;
        remaining--;
    }

    *dst = '\0';
    return ESP_OK;
}

esp_err_t display_config_get_variables(const char ***vars, size_t *count) {
    if (!vars || !count) return ESP_ERR_INVALID_ARG;

    static const char *var_list[100]; // assume max 100 variables
    size_t i = 0;
    for (const var_entry_t *e = variables; e->var_name && i < 100; ++e) {
        var_list[i++] = e->var_name;
    }

    *vars = var_list;
    *count = i;
    return ESP_OK;
}
