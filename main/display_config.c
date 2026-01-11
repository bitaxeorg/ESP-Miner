#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
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

static void handle_hashrate(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%.2f", GLOBAL_STATE->SYSTEM_MODULE.current_hashrate);
}

static void handle_hashrate_1m(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%.2f", GLOBAL_STATE->SYSTEM_MODULE.hashrate_1m);
}

static void handle_hashrate_10m(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%.2f", GLOBAL_STATE->SYSTEM_MODULE.hashrate_10m);
}

static void handle_hashrate_1h(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%.2f", GLOBAL_STATE->SYSTEM_MODULE.hashrate_1h);
}

static void handle_hashrate_expected(GlobalState *GLOBAL_STATE, char *temp, size_t ts,char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%.1f", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate);
}

static void handle_power(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%.1f", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.power);
}

static void handle_asic_temp(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%.1f", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.chip_temp_avg);
}

static void handle_asic2_temp(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%.1f", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.chip_temp2_avg);
}

static void handle_best_diff(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_string(dst, remaining, GLOBAL_STATE->SYSTEM_MODULE.best_diff_string);
}

static void handle_session_diff(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_string(dst, remaining, GLOBAL_STATE->SYSTEM_MODULE.best_session_diff_string);
}

static void handle_ip(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_string(dst, remaining, GLOBAL_STATE->SYSTEM_MODULE.ip_addr_str);
}

static void handle_ipv6(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_string(dst, remaining, GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str);
}

static void handle_shares_a(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%llu", GLOBAL_STATE->SYSTEM_MODULE.shares_accepted);
}

static void handle_shares_r(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%llu", GLOBAL_STATE->SYSTEM_MODULE.shares_rejected);
}

static void handle_network_diff(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_string(dst, remaining, GLOBAL_STATE->network_diff_string);
}

static void handle_scriptsig(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_string(dst, remaining, GLOBAL_STATE->scriptsig);
}

static void handle_voltage(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%.2f", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.voltage);
}

static void handle_core_voltage(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%.2f", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.core_voltage);
}

static void handle_current(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%.2f", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.current);
}

static void handle_fan_perc(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%.1f", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan_perc);
}

static void handle_fan_rpm(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%u", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan_rpm);
}

static void handle_fan2_rpm(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%u", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan2_rpm);
}

static void handle_work_received(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%llu", GLOBAL_STATE->SYSTEM_MODULE.work_received);
}

static void handle_response_time(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%.2f", GLOBAL_STATE->SYSTEM_MODULE.response_time);
}

static void handle_frequency(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%.0f", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value);
}

static void handle_vr_temp(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%.1f", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.vr_temp);
}

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

    uint32_t days    = total_seconds / 86400;
    total_seconds   %= 86400;
    uint32_t hours   = total_seconds / 3600;
    total_seconds   %= 3600;
    uint32_t minutes = total_seconds / 60;
    uint32_t seconds = total_seconds % 60;

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

static void handle_error_percentage(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%.2f", GLOBAL_STATE->SYSTEM_MODULE.error_percentage);
}

static void handle_ssid(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_string(dst, remaining, GLOBAL_STATE->SYSTEM_MODULE.ssid);
}

static void handle_wifi_status(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_string(dst, remaining, GLOBAL_STATE->SYSTEM_MODULE.wifi_status);
}

static void handle_pool_connection_info(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_string(dst, remaining, GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info);
}

static void handle_power_fault(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%u", GLOBAL_STATE->SYSTEM_MODULE.power_fault);
}

static void handle_block_found(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    const char *found = GLOBAL_STATE->SYSTEM_MODULE.block_found ? "Yes" : "No";
    append_string(dst, remaining, found);
}

static void handle_hostname(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    char *hostname = nvs_config_get_string(NVS_CONFIG_HOSTNAME);
    append_string(dst, remaining, hostname);
    free(hostname);
}

static void handle_device_model(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_string(dst, remaining, GLOBAL_STATE->DEVICE_CONFIG.family.name);
}

static void handle_asic_model(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_string(dst, remaining, GLOBAL_STATE->DEVICE_CONFIG.family.asic.name);
}

static void handle_board_version(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_string(dst, remaining, GLOBAL_STATE->DEVICE_CONFIG.board_version);
}

static void handle_free_heap(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%u", esp_get_free_heap_size());
}

static void handle_version(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_string(dst, remaining, GLOBAL_STATE->SYSTEM_MODULE.version);
}

static void handle_axe_os_version(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_string(dst, remaining, GLOBAL_STATE->SYSTEM_MODULE.axeOSVersion);
}

static void handle_is_using_fallback_stratum(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    const char *str = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? "Yes" : "No";
    append_string(dst, remaining, str);
}

static void handle_pool_difficulty(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%u", GLOBAL_STATE->pool_difficulty);
}

static void handle_stratum_url(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    char *url = nvs_config_get_string(NVS_CONFIG_STRATUM_URL);
    append_string(dst, remaining, url);
    free(url);
}

static void handle_stratum_port(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    uint16_t port = nvs_config_get_u16(NVS_CONFIG_STRATUM_PORT);
    append_formatted(dst, remaining, temp, ts, "%u", port);
}

static void handle_stratum_user(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    char *user = nvs_config_get_string(NVS_CONFIG_STRATUM_USER);
    append_string(dst, remaining, user);
    free(user);
}

static void handle_block_height(GlobalState *GLOBAL_STATE, char *temp, size_t ts, char **dst, size_t *remaining)
{
    append_formatted(dst, remaining, temp, ts, "%u", GLOBAL_STATE->block_height);
}

typedef void (*handler_func_t)(GlobalState *GLOBAL_STATE, char *temp, size_t temp_size, char **dst, size_t *remaining);

typedef struct {
    const char    *var_name;
    handler_func_t handler;
} var_entry_t;

static const var_entry_t variables[] = {
    { "hashrate",          handle_hashrate },
    { "hashrate_1m",       handle_hashrate_1m },
    { "hashrate_10m",      handle_hashrate_10m },
    { "hashrate_1h",       handle_hashrate_1h },
    { "hashrate_expected", handle_hashrate_expected },
    
    { "frequency",         handle_frequency },
    { "power",             handle_power },
    { "efficiency",        handle_efficiency },
    { "voltage",           handle_voltage },
    { "core_voltage",      handle_core_voltage },
    { "current",           handle_current },
    { "power_fault",       handle_power_fault },

    { "asic_temp",         handle_asic_temp },
    { "asic2_temp",        handle_asic2_temp },
    { "vr_temp",           handle_vr_temp },
    { "target_temp",       handle_target_temp },

    { "fan_perc",          handle_fan_perc },
    { "fan_rpm",           handle_fan_rpm },
    { "fan2_rpm",          handle_fan2_rpm },

    { "pool_url",          handle_pool_url },
    { "pool_difficulty",   handle_pool_difficulty },
    { "stratum_url",       handle_stratum_url },
    { "stratum_port",      handle_stratum_port },
    { "stratum_user",      handle_stratum_user },
    { "response_time",     handle_response_time },
    { "pool_connection_info", handle_pool_connection_info },
    { "is_using_fallback_stratum", handle_is_using_fallback_stratum },

    { "shares_a",          handle_shares_a },
    { "shares_r",          handle_shares_r },
    { "work_received",     handle_work_received },
    { "error_percentage",  handle_error_percentage },
    { "session_diff",      handle_session_diff },
    { "best_diff",         handle_best_diff },   
    { "block_found",       handle_block_found },

    { "ssid",              handle_ssid },
    { "wifi_status",       handle_wifi_status },
    { "ip",                handle_ip },
    { "ipv6",              handle_ipv6 },
    { "rssi",              handle_rssi },
    { "signal",            handle_signal },
    { "uptime",            handle_uptime },

    { "network_diff",      handle_network_diff },
    { "scriptsig",         handle_scriptsig },
    { "block_height",      handle_block_height },

    { "hostname",          handle_hostname },
    { "device_model",      handle_device_model },
    { "asic_model",        handle_asic_model },
    { "board_version",     handle_board_version },

    { "version",           handle_version },
    { "axe_os_version",    handle_axe_os_version },
    { "free_heap",         handle_free_heap },
    { NULL,                NULL }
};

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

                    bool found = false;
                    for (const var_entry_t *e = variables; e->var_name; ++e) {
                        if (strcmp(var_buffer, e->var_name) == 0) {
                            e->handler(GLOBAL_STATE, temp_buffer, sizeof(temp_buffer),
                                       &dst, &remaining);
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
