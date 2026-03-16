#include "miner_adapter.h"
#include "gateway_task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "cJSON.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ctype.h>

static const char *TAG = "miner_adapter";

#define HTTP_RECV_BUF_SIZE 4096
#define CGMINER_PORT 4028
#define CGMINER_RECV_BUF_SIZE 4096

typedef struct {
    char *buf;
    int len;
    int capacity;
} http_response_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_t *resp = (http_response_t *)evt->user_data;
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (resp->len + evt->data_len < resp->capacity) {
            memcpy(resp->buf + resp->len, evt->data, evt->data_len);
            resp->len += evt->data_len;
            resp->buf[resp->len] = '\0';
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

const char *miner_type_to_string(MinerType type)
{
    switch (type) {
    case MINER_TYPE_BITAXE: return "bitaxe";
    case MINER_TYPE_HAMMER: return "hammer";
    case MINER_TYPE_NERD:   return "nerd";
    case MINER_TYPE_CANAAN: return "canaan";
    default:                return "unknown";
    }
}

MinerType miner_type_from_string(const char *str)
{
    if (!str) return MINER_TYPE_UNKNOWN;
    if (strcasecmp(str, "bitaxe") == 0 || strcasecmp(str, "AxeOs") == 0) return MINER_TYPE_BITAXE;
    if (strcasecmp(str, "hammer") == 0 || strcasecmp(str, "Hammer") == 0) return MINER_TYPE_HAMMER;
    if (strcasecmp(str, "nerd") == 0   || strcasecmp(str, "Nerd") == 0)   return MINER_TYPE_NERD;
    if (strcasecmp(str, "canaan") == 0 ||
        strcasecmp(str, "AvalonNano3s") == 0 ||
        strcasecmp(str, "AvalonQ") == 0 ||
        strcasecmp(str, "AvalonMini3") == 0) return MINER_TYPE_CANAAN;
    return MINER_TYPE_UNKNOWN;
}

// Send a cgminer API command over TCP socket and read the response.
// connect_timeout_ms controls how long to wait for the TCP connection (0 = default 3s).
static bool cgminer_command_timeout(const char *ip, const char *cmd, char *response,
                                     int resp_size, int connect_timeout_ms)
{
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(CGMINER_PORT);
    inet_pton(AF_INET, ip, &dest_addr.sin_addr);

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        return false;
    }

    if (connect_timeout_ms > 0) {
        // Non-blocking connect with timeout
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);

        int ret = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (ret != 0 && errno == EINPROGRESS) {
            fd_set fdset;
            FD_ZERO(&fdset);
            FD_SET(sock, &fdset);
            struct timeval tv = {
                .tv_sec = connect_timeout_ms / 1000,
                .tv_usec = (connect_timeout_ms % 1000) * 1000,
            };
            ret = select(sock + 1, NULL, &fdset, NULL, &tv);
            if (ret <= 0) {
                close(sock);
                return false;
            }
            int so_error;
            socklen_t len = sizeof(so_error);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
            if (so_error != 0) {
                close(sock);
                return false;
            }
        } else if (ret != 0) {
            close(sock);
            return false;
        }

        // Back to blocking for send/recv
        fcntl(sock, F_SETFL, flags);
    } else {
        if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
            close(sock);
            return false;
        }
    }

    // Set send/recv timeout
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int sent = send(sock, cmd, strlen(cmd), 0);
    if (sent < 0) {
        close(sock);
        return false;
    }

    int total_read = 0;
    while (total_read < resp_size - 1) {
        int r = recv(sock, response + total_read, resp_size - 1 - total_read, 0);
        if (r <= 0) break;
        total_read += r;
    }
    response[total_read] = '\0';
    close(sock);

    return total_read > 0;
}

static bool cgminer_command(const char *ip, const char *cmd, char *response, int resp_size)
{
    return cgminer_command_timeout(ip, cmd, response, resp_size, 0);
}

// Detect miner type from AxeOS JSON response
static MinerType detect_axeos_type(cJSON *root)
{
    cJSON *axeos_version = cJSON_GetObjectItem(root, "axeOSVersion");
    cJSON *device_model_cap = cJSON_GetObjectItem(root, "DeviceModel");
    cJSON *device_model_low = cJSON_GetObjectItem(root, "deviceModel");

    if (cJSON_IsString(device_model_low) && strstr(device_model_low->valuestring, "Nerd") != NULL) {
        return MINER_TYPE_NERD;
    }
    if (cJSON_IsString(axeos_version)) {
        return MINER_TYPE_BITAXE;
    }
    if (cJSON_IsString(device_model_cap) && !cJSON_IsString(axeos_version)) {
        return MINER_TYPE_HAMMER;
    }
    return MINER_TYPE_BITAXE; // default for AxeOS-family
}

// Parse difficulty strings like "955M", "2.62G", "484.82M" to a number.
// Also handles plain numbers (returns them as-is via strtod).
static double parse_diff_string(const char *str)
{
    if (!str) return 0;
    char *endptr;
    double val = strtod(str, &endptr);
    if (endptr == str) return 0;
    switch (*endptr) {
        case 'k': case 'K': return val * 1e3;
        case 'M': return val * 1e6;
        case 'G': return val * 1e9;
        case 'T': return val * 1e12;
        case 'P': return val * 1e15;
        default:  return val;
    }
}

// Extract bestDiff from a cJSON item that may be a number or a string like "955M"
static double extract_diff(cJSON *item)
{
    if (cJSON_IsNumber(item)) return item->valuedouble;
    if (cJSON_IsString(item)) return parse_diff_string(item->valuestring);
    return 0;
}

// Poll an AxeOS-family miner (Bitaxe, Hammer, Nerd) via HTTP
static bool poll_axeos(const char *ip, PeerMinerState *peer, int timeout_ms)
{
    char url[64];
    snprintf(url, sizeof(url), "http://%s/api/system/info", ip);

    char *recv_buf = heap_caps_malloc(HTTP_RECV_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!recv_buf) {
        ESP_LOGE(TAG, "Failed to alloc recv buffer for peer %s", ip);
        return false;
    }

    http_response_t resp = {
        .buf = recv_buf,
        .len = 0,
        .capacity = HTTP_RECV_BUF_SIZE
    };

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = timeout_ms,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .disable_auto_redirect = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client for peer %s", ip);
        free(recv_buf);
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status_code != 200 || resp.len == 0) {
        ESP_LOGW(TAG, "Peer %s AxeOS poll failed: err=%d status=%d len=%d", ip, err, status_code, resp.len);
        free(recv_buf);
        return false;
    }

    cJSON *root = cJSON_Parse(recv_buf);
    free(recv_buf);

    if (!root) {
        ESP_LOGW(TAG, "Peer %s: failed to parse JSON", ip);
        return false;
    }

    // Auto-detect type from response
    peer->type = detect_axeos_type(root);

    cJSON *item;

    item = cJSON_GetObjectItem(root, "hashRate");
    if (cJSON_IsNumber(item)) peer->hashrate = (float)item->valuedouble;

    // Temp: compute avg/max from temp + temp2
    float t1 = 0, t2 = 0;
    item = cJSON_GetObjectItem(root, "temp");
    if (cJSON_IsNumber(item)) { t1 = (float)item->valuedouble; peer->temp = t1; }
    item = cJSON_GetObjectItem(root, "temp2");
    if (cJSON_IsNumber(item) && item->valuedouble > 0) t2 = (float)item->valuedouble;
    if (t1 > 0 && t2 > 0) {
        peer->temp_avg = (t1 + t2) / 2.0f;
        peer->temp_max = t1 > t2 ? t1 : t2;
    } else if (t1 > 0) {
        peer->temp_avg = t1;
        peer->temp_max = t1;
    }

    item = cJSON_GetObjectItem(root, "power");
    if (cJSON_IsNumber(item)) peer->power = (float)item->valuedouble;

    item = cJSON_GetObjectItem(root, "coreVoltageActual");
    if (cJSON_IsNumber(item)) peer->voltage = (float)item->valuedouble;

    item = cJSON_GetObjectItem(root, "frequency");
    if (cJSON_IsNumber(item)) peer->frequency = (float)item->valuedouble;

    item = cJSON_GetObjectItem(root, "sharesAccepted");
    if (cJSON_IsNumber(item)) peer->shares_accepted = (uint64_t)item->valuedouble;

    item = cJSON_GetObjectItem(root, "sharesRejected");
    if (cJSON_IsNumber(item)) peer->shares_rejected = (uint64_t)item->valuedouble;

    item = cJSON_GetObjectItem(root, "uptimeSeconds");
    if (cJSON_IsNumber(item)) peer->uptime_seconds = (uint32_t)item->valuedouble;

    item = cJSON_GetObjectItem(root, "macAddr");
    if (cJSON_IsString(item)) strncpy(peer->mac_addr, item->valuestring, sizeof(peer->mac_addr) - 1);

    item = cJSON_GetObjectItem(root, "hostname");
    if (cJSON_IsString(item)) strncpy(peer->hostname, item->valuestring, sizeof(peer->hostname) - 1);

    item = cJSON_GetObjectItem(root, "ASICModel");
    if (cJSON_IsString(item)) strncpy(peer->asic_model, item->valuestring, sizeof(peer->asic_model) - 1);

    // Device/product model — "deviceModel" (Nerd) or "DeviceModel" (Hammer)
    item = cJSON_GetObjectItem(root, "deviceModel");
    if (!cJSON_IsString(item)) item = cJSON_GetObjectItem(root, "DeviceModel");
    if (cJSON_IsString(item)) strncpy(peer->device_model, item->valuestring, sizeof(peer->device_model) - 1);

    item = cJSON_GetObjectItem(root, "vrTemp");
    if (cJSON_IsNumber(item)) peer->vr_temp = (float)item->valuedouble;

    item = cJSON_GetObjectItem(root, "fanspeed");
    if (cJSON_IsNumber(item)) peer->fan_pct = (int)item->valuedouble;

    // Fan RPM: average of fanrpm and fan2rpm (fan2rpm may be 0 if single fan)
    {
        int rpm_sum = 0, rpm_count = 0;
        item = cJSON_GetObjectItem(root, "fanrpm");
        if (cJSON_IsNumber(item) && item->valueint > 0) { rpm_sum += item->valueint; rpm_count++; }
        item = cJSON_GetObjectItem(root, "fan2rpm");
        if (cJSON_IsNumber(item) && item->valueint > 0) { rpm_sum += item->valueint; rpm_count++; }
        if (rpm_count > 0) peer->fan_rpm = rpm_sum / rpm_count;
    }

    item = cJSON_GetObjectItem(root, "wifiRSSI");
    if (cJSON_IsNumber(item)) peer->wifi_rssi = (int)item->valuedouble;

    // bestDiff: number on Bitaxe, string like "955M" on Nerd
    peer->best_diff = extract_diff(cJSON_GetObjectItem(root, "bestDiff"));
    peer->best_session_diff = extract_diff(cJSON_GetObjectItem(root, "bestSessionDiff"));

    item = cJSON_GetObjectItem(root, "poolDifficulty");
    if (cJSON_IsNumber(item)) peer->pool_difficulty = item->valuedouble;

    // foundBlocks: "blockFound" on Bitaxe, "foundBlocks" on Nerd
    item = cJSON_GetObjectItem(root, "foundBlocks");
    if (!cJSON_IsNumber(item)) item = cJSON_GetObjectItem(root, "blockFound");
    if (cJSON_IsNumber(item)) peer->found_blocks = (int)item->valuedouble;

    // Pools: primary + fallback
    peer->pool_count = 0;
    bool using_fallback = false;
    item = cJSON_GetObjectItem(root, "isUsingFallbackStratum");
    if (cJSON_IsNumber(item)) using_fallback = item->valuedouble != 0;
    if (cJSON_IsBool(item)) using_fallback = cJSON_IsTrue(item);

    cJSON *surl = cJSON_GetObjectItem(root, "stratumURL");
    cJSON *sport = cJSON_GetObjectItem(root, "stratumPort");
    cJSON *suser = cJSON_GetObjectItem(root, "stratumUser");
    if (cJSON_IsString(surl) && strlen(surl->valuestring) > 0) {
        PoolInfo *p = &peer->pools[peer->pool_count];
        memset(p, 0, sizeof(PoolInfo));
        if (cJSON_IsNumber(sport))
            snprintf(p->url, sizeof(p->url), "%s:%d", surl->valuestring, (int)sport->valuedouble);
        else
            strncpy(p->url, surl->valuestring, sizeof(p->url) - 1);
        if (cJSON_IsString(suser)) strncpy(p->user, suser->valuestring, sizeof(p->user) - 1);
        p->active = !using_fallback;
        if (p->active) p->stratum_diff = peer->pool_difficulty;
        peer->pool_count++;
    }

    cJSON *furl = cJSON_GetObjectItem(root, "fallbackStratumURL");
    cJSON *fport = cJSON_GetObjectItem(root, "fallbackStratumPort");
    cJSON *fuser = cJSON_GetObjectItem(root, "fallbackStratumUser");
    if (cJSON_IsString(furl) && strlen(furl->valuestring) > 0) {
        PoolInfo *p = &peer->pools[peer->pool_count];
        memset(p, 0, sizeof(PoolInfo));
        if (cJSON_IsNumber(fport))
            snprintf(p->url, sizeof(p->url), "%s:%d", furl->valuestring, (int)fport->valuedouble);
        else
            strncpy(p->url, furl->valuestring, sizeof(p->url) - 1);
        if (cJSON_IsString(fuser)) strncpy(p->user, fuser->valuestring, sizeof(p->user) - 1);
        p->active = using_fallback;
        if (p->active) p->stratum_diff = peer->pool_difficulty;
        peer->pool_count++;
    }

    peer->last_seen = esp_timer_get_time();

    cJSON_Delete(root);
    return true;
}

// Parse a cgminer JSON response - cgminer wraps responses with a null byte at the end
// and sometimes uses non-standard JSON. We strip trailing null bytes and parse.
static cJSON *parse_cgminer_response(const char *response)
{
    if (!response || strlen(response) == 0) return NULL;
    return cJSON_Parse(response);
}

// Extract a value from MM ID0 bracketed format: "Key[value]"
// e.g. mmget("Fan1[1520] FanR[85] WORKMODE[1]", "Fan1", buf, 32) -> "1520"
bool mmget(const char *mmid0, const char *key, char *out, int out_size)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "%s[", key);
    const char *start = strstr(mmid0, pattern);
    if (!start) return false;
    start += strlen(pattern);
    const char *end = strchr(start, ']');
    if (!end || (end - start) >= out_size) return false;
    int len = (int)(end - start);
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

// Poll a Canaan miner via cgminer API on port 4028
static bool poll_canaan(const char *ip, PeerMinerState *peer)
{
    char *resp_buf = heap_caps_malloc(CGMINER_RECV_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!resp_buf) {
        ESP_LOGE(TAG, "Failed to alloc cgminer recv buffer for %s", ip);
        return false;
    }

    // Get version info (model + MAC)
    if (cgminer_command(ip, "{\"command\":\"version\"}", resp_buf, CGMINER_RECV_BUF_SIZE)) {
        cJSON *root = parse_cgminer_response(resp_buf);
        if (root) {
            cJSON *version_arr = cJSON_GetObjectItem(root, "VERSION");
            if (cJSON_IsArray(version_arr) && cJSON_GetArraySize(version_arr) > 0) {
                cJSON *ver = cJSON_GetArrayItem(version_arr, 0);
                cJSON *item = cJSON_GetObjectItem(ver, "PROD");
                if (cJSON_IsString(item)) {
                    // PROD is the product/device model for Canaan (e.g. "Avalon Nano3s")
                    strncpy(peer->device_model, item->valuestring, sizeof(peer->device_model) - 1);
                    strncpy(peer->asic_model, item->valuestring, sizeof(peer->asic_model) - 1);
                }
                // Fallback to Type if PROD is missing
                if (strlen(peer->asic_model) == 0) {
                    item = cJSON_GetObjectItem(ver, "Type");
                    if (cJSON_IsString(item)) {
                        strncpy(peer->asic_model, item->valuestring, sizeof(peer->asic_model) - 1);
                    }
                }
                // Extract MAC address (12-char hex string -> XX:XX:XX:XX:XX:XX)
                item = cJSON_GetObjectItem(ver, "MAC");
                if (cJSON_IsString(item) && strlen(peer->mac_addr) == 0) {
                    const char *raw = item->valuestring;
                    if (strlen(raw) == 12) {
                        snprintf(peer->mac_addr, sizeof(peer->mac_addr),
                                 "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
                                 toupper(raw[0]), toupper(raw[1]),
                                 toupper(raw[2]), toupper(raw[3]),
                                 toupper(raw[4]), toupper(raw[5]),
                                 toupper(raw[6]), toupper(raw[7]),
                                 toupper(raw[8]), toupper(raw[9]),
                                 toupper(raw[10]), toupper(raw[11]));
                    } else {
                        strncpy(peer->mac_addr, raw, sizeof(peer->mac_addr) - 1);
                    }
                }
            }
            cJSON_Delete(root);
        }
    }

    // Get summary info (hashrate, shares)
    bool got_summary = false;
    if (cgminer_command(ip, "{\"command\":\"summary\"}", resp_buf, CGMINER_RECV_BUF_SIZE)) {
        cJSON *root = parse_cgminer_response(resp_buf);
        if (root) {
            cJSON *summary_arr = cJSON_GetObjectItem(root, "SUMMARY");
            if (cJSON_IsArray(summary_arr) && cJSON_GetArraySize(summary_arr) > 0) {
                cJSON *summary = cJSON_GetArrayItem(summary_arr, 0);

                cJSON *item = cJSON_GetObjectItem(summary, "MHS 1m");
                if (cJSON_IsNumber(item)) {
                    // Convert MH/s to GH/s
                    peer->hashrate = (float)(item->valuedouble / 1000.0);
                }

                item = cJSON_GetObjectItem(summary, "Accepted");
                if (cJSON_IsNumber(item)) peer->shares_accepted = (uint64_t)item->valuedouble;

                item = cJSON_GetObjectItem(summary, "Rejected");
                if (cJSON_IsNumber(item)) peer->shares_rejected = (uint64_t)item->valuedouble;

                item = cJSON_GetObjectItem(summary, "Elapsed");
                if (cJSON_IsNumber(item)) peer->uptime_seconds = (uint32_t)item->valuedouble;

                item = cJSON_GetObjectItem(summary, "Found Blocks");
                if (cJSON_IsNumber(item)) peer->found_blocks = (int)item->valuedouble;

                item = cJSON_GetObjectItem(summary, "Best Share");
                if (cJSON_IsNumber(item)) peer->best_diff = item->valuedouble;

                got_summary = true;
            }
            cJSON_Delete(root);
        }
    }

    // Get estats info (temp, power, frequency, fan, work mode from MM ID0)
    if (cgminer_command(ip, "{\"command\":\"estats\"}", resp_buf, CGMINER_RECV_BUF_SIZE)) {
        cJSON *root = parse_cgminer_response(resp_buf);
        if (root) {
            cJSON *stats_arr = cJSON_GetObjectItem(root, "STATS");
            if (cJSON_IsArray(stats_arr) && cJSON_GetArraySize(stats_arr) > 0) {
                cJSON *stats = cJSON_GetArrayItem(stats_arr, 0);

                // PROD fallback
                cJSON *item = cJSON_GetObjectItem(stats, "PROD");
                if (cJSON_IsString(item) && strlen(peer->asic_model) == 0) {
                    strncpy(peer->asic_model, item->valuestring, sizeof(peer->asic_model) - 1);
                }

                // Get MM ID0 string (Nano3S/Mini3) or "MM ID0:Summary" (AvalonQ)
                // This contains bracketed key-value data like Fan1[1520] FanR[85] PS[0 0 0 0 0 0 133]
                cJSON *mmid0_item = cJSON_GetObjectItem(stats, "MM ID0");
                if (!cJSON_IsString(mmid0_item)) {
                    mmid0_item = cJSON_GetObjectItem(stats, "MM ID0:Summary");
                }

                if (cJSON_IsString(mmid0_item)) {
                    const char *mmid0 = mmid0_item->valuestring;
                    char val[64];

                    // ITemp: internal controller board temperature (not chip temp)
                    if (mmget(mmid0, "ITemp", val, sizeof(val))) {
                        float t = (float)atof(val);
                        if (t > -100) { // -273 means unavailable
                            peer->temp = t; // fallback for main temp display
                        }
                    }

                    // Fan speed percentage
                    if (mmget(mmid0, "FanR", val, sizeof(val))) {
                        peer->fan_pct = atoi(val);
                    }

                    // Fan RPM: average of Fan1-Fan4
                    {
                        int rpm_sum = 0, rpm_count = 0;
                        const char *fan_keys[] = {"Fan1", "Fan2", "Fan3", "Fan4"};
                        for (int fi = 0; fi < 4; fi++) {
                            if (mmget(mmid0, fan_keys[fi], val, sizeof(val))) {
                                int rpm = atoi(val);
                                if (rpm > 0) { rpm_sum += rpm; rpm_count++; }
                            }
                        }
                        if (rpm_count > 0) peer->fan_rpm = rpm_sum / rpm_count;
                    }

                    // Frequency
                    if (mmget(mmid0, "Freq", val, sizeof(val))) {
                        peer->frequency = (float)atof(val);
                    }

                    // Power from PS field (7th value is watts)
                    // PS[0 1208 2469 64 1604 2470 1381]
                    if (mmget(mmid0, "PS", val, sizeof(val))) {
                        int idx = 0;
                        char *tok = val;
                        char *saveptr = NULL;
                        tok = strtok_r(val, " ", &saveptr);
                        while (tok) {
                            if (idx == 6) {
                                peer->power = (float)atof(tok);
                                break;
                            }
                            tok = strtok_r(NULL, " ", &saveptr);
                            idx++;
                        }
                    }

                    // Aggregated chip temperatures from TAvg/TMax
                    if (mmget(mmid0, "TAvg", val, sizeof(val))) {
                        float t = (float)atof(val);
                        if (t > 0) { peer->temp_avg = t; peer->temp = t; }
                    }
                    if (mmget(mmid0, "TMax", val, sizeof(val))) {
                        float t = (float)atof(val);
                        if (t > 0) peer->temp_max = t;
                    }
                } else {
                    // Fallback: try top-level fields
                    item = cJSON_GetObjectItem(stats, "ITemp");
                    if (cJSON_IsNumber(item)) peer->temp = (float)item->valuedouble;

                    item = cJSON_GetObjectItem(stats, "Frequency");
                    if (cJSON_IsNumber(item)) peer->frequency = (float)item->valuedouble;
                }
            }
            cJSON_Delete(root);
        }
    }

    // Get pool info (all pools with status + difficulty)
    peer->pool_count = 0;
    if (cgminer_command(ip, "{\"command\":\"pools\"}", resp_buf, CGMINER_RECV_BUF_SIZE)) {
        cJSON *root = parse_cgminer_response(resp_buf);
        if (root) {
            cJSON *pools_arr = cJSON_GetObjectItem(root, "POOLS");
            int npool = cJSON_IsArray(pools_arr) ? cJSON_GetArraySize(pools_arr) : 0;
            if (npool > MAX_POOLS) npool = MAX_POOLS;

            for (int pi = 0; pi < npool; pi++) {
                cJSON *pool = cJSON_GetArrayItem(pools_arr, pi);
                PoolInfo *p = &peer->pools[peer->pool_count];
                memset(p, 0, sizeof(PoolInfo));

                cJSON *url_item = cJSON_GetObjectItem(pool, "URL");
                if (cJSON_IsString(url_item))
                    strncpy(p->url, url_item->valuestring, sizeof(p->url) - 1);

                cJSON *user_item = cJSON_GetObjectItem(pool, "User");
                if (cJSON_IsString(user_item))
                    strncpy(p->user, user_item->valuestring, sizeof(p->user) - 1);

                cJSON *active_item = cJSON_GetObjectItem(pool, "Stratum Active");
                p->active = cJSON_IsTrue(active_item);

                cJSON *diff_item = cJSON_GetObjectItem(pool, "Stratum Difficulty");
                if (cJSON_IsNumber(diff_item)) p->stratum_diff = diff_item->valuedouble;

                cJSON *acc_item = cJSON_GetObjectItem(pool, "Accepted");
                if (cJSON_IsNumber(acc_item)) p->accepted = (uint64_t)acc_item->valuedouble;

                cJSON *rej_item = cJSON_GetObjectItem(pool, "Rejected");
                if (cJSON_IsNumber(rej_item)) p->rejected = (uint64_t)rej_item->valuedouble;

                // Set pool_difficulty from the active pool
                if (p->active && p->stratum_diff > 0) {
                    peer->pool_difficulty = p->stratum_diff;
                }

                peer->pool_count++;
            }
            cJSON_Delete(root);
        }
    }

    free(resp_buf);

    if (!got_summary) {
        return false;
    }

    peer->type = MINER_TYPE_CANAAN;
    peer->last_seen = esp_timer_get_time();
    return true;
}

bool poll_miner(const char *ip, MinerType type, PeerMinerState *peer)
{
    switch (type) {
    case MINER_TYPE_BITAXE:
    case MINER_TYPE_HAMMER:
    case MINER_TYPE_NERD:
    case MINER_TYPE_UNKNOWN:
        return poll_axeos(ip, peer, 5000);
    case MINER_TYPE_CANAAN:
        return poll_canaan(ip, peer);
    default:
        return poll_axeos(ip, peer, 5000);
    }
}

bool scan_ip(const char *ip, DiscoveredMiner *result)
{
    memset(result, 0, sizeof(DiscoveredMiner));
    strncpy(result->ip, ip, sizeof(result->ip) - 1);

    // Try AxeOS HTTP endpoint directly (no TCP pre-probe).
    // esp_http_client handles connection internally and is more robust
    // on ESP32 than raw socket open/close cycles.
    {
        char url[64];
        snprintf(url, sizeof(url), "http://%s/api/system/info", ip);

        char *recv_buf = heap_caps_malloc(HTTP_RECV_BUF_SIZE, MALLOC_CAP_SPIRAM);
        if (!recv_buf) return false;

        http_response_t resp = {
            .buf = recv_buf,
            .len = 0,
            .capacity = HTTP_RECV_BUF_SIZE
        };

        esp_http_client_config_t config = {
            .url = url,
            .timeout_ms = 500,
            .event_handler = http_event_handler,
            .user_data = &resp,
            .disable_auto_redirect = true,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client) {
            esp_err_t err = esp_http_client_perform(client);
            int status_code = esp_http_client_get_status_code(client);
            esp_http_client_cleanup(client);

            if (err == ESP_OK && status_code == 200 && resp.len > 0) {
                cJSON *root = cJSON_Parse(recv_buf);
                free(recv_buf);

                if (root) {
                    result->type = detect_axeos_type(root);
                    result->reachable = true;

                    cJSON *item;
                    item = cJSON_GetObjectItem(root, "macAddr");
                    if (cJSON_IsString(item)) strncpy(result->mac, item->valuestring, sizeof(result->mac) - 1);

                    item = cJSON_GetObjectItem(root, "ASICModel");
                    if (cJSON_IsString(item)) strncpy(result->model, item->valuestring, sizeof(result->model) - 1);

                    item = cJSON_GetObjectItem(root, "hostname");
                    if (cJSON_IsString(item)) strncpy(result->hostname, item->valuestring, sizeof(result->hostname) - 1);

                    item = cJSON_GetObjectItem(root, "hashRate");
                    if (cJSON_IsNumber(item)) result->hashrate = (float)item->valuedouble;

                    cJSON_Delete(root);
                    return true;
                }
            } else {
                free(recv_buf);
            }
        } else {
            free(recv_buf);
        }
    }

    // Try cgminer API directly on port 4028 (for Canaan/Avalon miners).
    // Uses 500ms connect timeout so unreachable IPs fail fast.
    {
        char *cgminer_buf = heap_caps_malloc(CGMINER_RECV_BUF_SIZE, MALLOC_CAP_SPIRAM);
        if (cgminer_buf) {
            if (cgminer_command_timeout(ip, "{\"command\":\"version\"}", cgminer_buf, CGMINER_RECV_BUF_SIZE, 500)) {
                result->type = MINER_TYPE_CANAAN;
                result->reachable = true;

                cJSON *root = parse_cgminer_response(cgminer_buf);
                if (root) {
                    cJSON *version_arr = cJSON_GetObjectItem(root, "VERSION");
                    if (cJSON_IsArray(version_arr) && cJSON_GetArraySize(version_arr) > 0) {
                        cJSON *ver = cJSON_GetArrayItem(version_arr, 0);
                        cJSON *item = cJSON_GetObjectItem(ver, "Type");
                        if (cJSON_IsString(item)) {
                            strncpy(result->model, item->valuestring, sizeof(result->model) - 1);
                        }
                    }
                    cJSON_Delete(root);
                }
            }
            free(cgminer_buf);

            if (result->reachable) return true;
        }
    }

    return false;
}

static inline void ip_to_str(uint32_t ip, char *buf, int buf_size)
{
    snprintf(buf, buf_size, "%d.%d.%d.%d",
             (int)((ip >> 24) & 0xFF), (int)((ip >> 16) & 0xFF),
             (int)((ip >> 8) & 0xFF),  (int)(ip & 0xFF));
}

// Scan error codes for diagnostics
typedef enum {
    SCAN_OK = 0,
    SCAN_ERR_ALLOC = 1,
    SCAN_ERR_INIT = 2,
    SCAN_ERR_PERFORM = 3,
    SCAN_ERR_STATUS = 4,
    SCAN_ERR_PARSE = 5,
} ScanError;

// cgminer probe for network scanning (port 4028). Uses 500ms connect timeout.
// Returns true if a cgminer device was found.
static bool scan_ip_cgminer(const char *ip, DiscoveredMiner *result)
{
    memset(result, 0, sizeof(DiscoveredMiner));
    strncpy(result->ip, ip, sizeof(result->ip) - 1);

    char *cgminer_buf = heap_caps_malloc(CGMINER_RECV_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!cgminer_buf) return false;

    if (!cgminer_command_timeout(ip, "{\"command\":\"version\"}", cgminer_buf, CGMINER_RECV_BUF_SIZE, 500)) {
        free(cgminer_buf);
        return false;
    }

    result->type = MINER_TYPE_CANAAN;
    result->reachable = true;

    cJSON *root = parse_cgminer_response(cgminer_buf);
    free(cgminer_buf);
    if (root) {
        cJSON *version_arr = cJSON_GetObjectItem(root, "VERSION");
        if (cJSON_IsArray(version_arr) && cJSON_GetArraySize(version_arr) > 0) {
            cJSON *ver = cJSON_GetArrayItem(version_arr, 0);
            cJSON *item = cJSON_GetObjectItem(ver, "PROD");
            if (cJSON_IsString(item))
                strncpy(result->model, item->valuestring, sizeof(result->model) - 1);
            item = cJSON_GetObjectItem(ver, "MAC");
            if (cJSON_IsString(item)) {
                // Format MAC from "e0e1a93e2c67" to "E0:E1:A9:3E:2C:67"
                const char *raw = item->valuestring;
                if (strlen(raw) == 12) {
                    snprintf(result->mac, sizeof(result->mac),
                             "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
                             toupper(raw[0]), toupper(raw[1]),
                             toupper(raw[2]), toupper(raw[3]),
                             toupper(raw[4]), toupper(raw[5]),
                             toupper(raw[6]), toupper(raw[7]),
                             toupper(raw[8]), toupper(raw[9]),
                             toupper(raw[10]), toupper(raw[11]));
                } else {
                    strncpy(result->mac, raw, sizeof(result->mac) - 1);
                }
            }
        }
        cJSON_Delete(root);
    }
    return true;
}

// HTTP-only probe for network scanning. Uses 500ms timeout and
// only checks AxeOS endpoint.
// Returns SCAN_OK if a miner was found, error code otherwise.
static ScanError scan_ip_http_only(const char *ip, DiscoveredMiner *result)
{
    memset(result, 0, sizeof(DiscoveredMiner));
    strncpy(result->ip, ip, sizeof(result->ip) - 1);

    char url[64];
    snprintf(url, sizeof(url), "http://%s/api/system/info", ip);

    char *recv_buf = heap_caps_malloc(HTTP_RECV_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!recv_buf) return SCAN_ERR_ALLOC;

    http_response_t resp = {
        .buf = recv_buf,
        .len = 0,
        .capacity = HTTP_RECV_BUF_SIZE
    };

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 500,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .disable_auto_redirect = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(recv_buf);
        return SCAN_ERR_INIT;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        free(recv_buf);
        return SCAN_ERR_PERFORM;
    }

    if (status_code != 200 || resp.len == 0) {
        free(recv_buf);
        return SCAN_ERR_STATUS;
    }

    cJSON *root = cJSON_Parse(recv_buf);
    free(recv_buf);
    if (!root) return SCAN_ERR_PARSE;

    result->type = detect_axeos_type(root);
    result->reachable = true;

    cJSON *item;
    item = cJSON_GetObjectItem(root, "macAddr");
    if (cJSON_IsString(item)) strncpy(result->mac, item->valuestring, sizeof(result->mac) - 1);

    item = cJSON_GetObjectItem(root, "ASICModel");
    if (cJSON_IsString(item)) strncpy(result->model, item->valuestring, sizeof(result->model) - 1);

    item = cJSON_GetObjectItem(root, "hostname");
    if (cJSON_IsString(item)) strncpy(result->hostname, item->valuestring, sizeof(result->hostname) - 1);

    item = cJSON_GetObjectItem(root, "hashRate");
    if (cJSON_IsNumber(item)) result->hashrate = (float)item->valuedouble;

    cJSON_Delete(root);
    return SCAN_OK;
}

// ── ARP sweep ──────────────────────────────────────────────────────
// Uses lwIP's etharp API to discover alive hosts on the /24 subnet.
// ARP is Layer 2 — every IP device MUST respond, including devices
// that block ICMP (e.g. Canaan/Avalon miners).
//
// Strategy: send ARP requests in batches of ARP_BATCH_SIZE (≤ ARP table
// size of 10), wait for replies, harvest the table, repeat. This avoids
// needing to increase ARP_TABLE_SIZE.

#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "esp_netif_net_stack.h"

#define ARP_BATCH_SIZE      32
#define ARP_BATCH_WAIT_MS   200

// Context for tcpip_callback — send a batch of ARP requests + harvest table
typedef struct {
    struct netif *netif;
    uint32_t subnet_base;   // host byte order
    uint32_t own_ip;        // host byte order
    int batch_start;
    int batch_end;
    bool *alive;            // pointer to caller's bitmap
    int *alive_count;       // pointer to caller's count
    bool done;              // set true when callback completes
} arp_batch_ctx_t;

static void arp_batch_cb(void *arg)
{
    arp_batch_ctx_t *ctx = (arp_batch_ctx_t *)arg;
    // Send ARP requests for this batch
    for (int host = ctx->batch_start; host <= ctx->batch_end; host++) {
        uint32_t target = ctx->subnet_base | host;
        if (target == ctx->own_ip) continue;
        ip4_addr_t addr;
        ip4_addr_set_u32(&addr, htonl(target));
        etharp_request(ctx->netif, &addr);
    }
    ctx->done = true;
}

static void arp_harvest_cb(void *arg)
{
    arp_batch_ctx_t *ctx = (arp_batch_ctx_t *)arg;
    for (size_t i = 0; i < ARP_TABLE_SIZE; i++) {
        ip4_addr_t *ip = NULL;
        struct netif *netif = NULL;
        struct eth_addr *eth = NULL;

        if (etharp_get_entry(i, &ip, &netif, &eth) && ip) {
            uint32_t addr = ntohl(ip4_addr_get_u32(ip));
            if ((addr & 0xFFFFFF00) == ctx->subnet_base) {
                int host = addr & 0xFF;
                if (host >= 1 && host <= 254 && !ctx->alive[host]) {
                    ctx->alive[host] = true;
                    (*ctx->alive_count)++;
                }
            }
        }
    }
    ctx->done = true;
}

/**
 * ARP sweep of a /24 subnet.
 * Sends ARP requests in batches and harvests the ARP table after each batch.
 * @param subnet_base  Network address in host byte order
 * @param own_ip       Our own IP in host byte order (skipped)
 * @param alive        Output bitmap, index 1-254. Must be zeroed by caller.
 * @param stop         Pointer to volatile stop flag
 * @return             Number of alive hosts found
 */
static int arp_sweep(uint32_t subnet_base, uint32_t own_ip,
                     bool alive[255], volatile bool *stop)
{
    // Get the WiFi station netif for lwIP
    esp_netif_t *esp_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!esp_netif) {
        ESP_LOGE(TAG, "ARP sweep: no WiFi netif");
        return -1;
    }
    struct netif *lwip_netif = (struct netif *)esp_netif_get_netif_impl(esp_netif);
    if (!lwip_netif) {
        ESP_LOGE(TAG, "ARP sweep: no lwIP netif");
        return -1;
    }

    int alive_count = 0;

    // Shared context for all batch callbacks — lives on our stack for the
    // duration of the sweep. Each callback sets done=true when finished.
    arp_batch_ctx_t ctx = {
        .netif = lwip_netif,
        .subnet_base = subnet_base,
        .own_ip = own_ip,
        .alive = alive,
        .alive_count = &alive_count,
        .done = false,
    };

    ESP_LOGI(TAG, "ARP sweep: scanning %d.%d.%d.0/24 in batches of %d...",
             (subnet_base >> 24) & 0xFF,
             (subnet_base >> 16) & 0xFF,
             (subnet_base >> 8) & 0xFF,
             ARP_BATCH_SIZE);

    for (int batch_start = 1; batch_start <= 254; batch_start += ARP_BATCH_SIZE) {
        if (stop && *stop) break;

        int batch_end = batch_start + ARP_BATCH_SIZE - 1;
        if (batch_end > 254) batch_end = 254;

        // Send ARP requests for this batch (runs in lwIP thread)
        ctx.batch_start = batch_start;
        ctx.batch_end = batch_end;
        ctx.done = false;
        tcpip_callback(arp_batch_cb, &ctx);
        while (!ctx.done) vTaskDelay(1);  // wait for send to complete

        // Wait for ARP replies
        vTaskDelay(pdMS_TO_TICKS(ARP_BATCH_WAIT_MS));

        // Harvest ARP table (runs in lwIP thread)
        ctx.done = false;
        tcpip_callback(arp_harvest_cb, &ctx);
        while (!ctx.done) vTaskDelay(1);  // wait for harvest to complete

        if (batch_start % 50 < ARP_BATCH_SIZE) {
            ESP_LOGI(TAG, "ARP sweep: %d/254, alive so far: %d",
                     batch_end, alive_count);
        }
    }

    ESP_LOGI(TAG, "ARP sweep: %d hosts alive", alive_count);
    return alive_count;
}

// ── Network scan (ARP sweep + targeted probes) ────────────────────

void run_network_scan(ScanState *state, scan_found_cb_t on_found, void *cb_ctx)
{
    ESP_LOGI(TAG, "Starting network scan (ARP sweep + targeted probes)...");
    state->result_count = 0;
    state->complete = false;

    // Get our own IP address
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        ESP_LOGE(TAG, "Network scan: could not get netif handle");
        state->scanning = false;
        state->complete = true;
        return;
    }

    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        ESP_LOGE(TAG, "Network scan: could not get IP info");
        state->scanning = false;
        state->complete = true;
        return;
    }

    uint32_t own_ip = ntohl(ip_info.ip.addr);
    uint32_t subnet_base = own_ip & 0xFFFFFF00;

    ESP_LOGI(TAG, "Scanning subnet %d.%d.%d.0/24 (own IP: %d.%d.%d.%d)",
             (subnet_base >> 24) & 0xFF,
             (subnet_base >> 16) & 0xFF,
             (subnet_base >> 8) & 0xFF,
             (own_ip >> 24) & 0xFF,
             (own_ip >> 16) & 0xFF,
             (own_ip >> 8) & 0xFF,
             own_ip & 0xFF);

    // Phase 1: ARP sweep to find alive hosts (catches ICMP-silent devices too)
    bool alive[255] = {0};
    int alive_count = arp_sweep(subnet_base, own_ip, alive, &state->stop_requested);

    if (alive_count < 0) {
        ESP_LOGW(TAG, "ARP sweep failed, falling back to full scan");
        for (int i = 1; i <= 254; i++) alive[i] = true;
        alive_count = 254;
    }

    if (!state->scanning || state->stop_requested) goto done;

    ESP_LOGI(TAG, "ARP sweep found %d alive hosts, probing with HTTP...", alive_count);

    // Phase 2: HTTP probe only alive hosts
    for (int host = 1; host <= 254 && state->result_count < SCAN_MAX_RESULTS; host++) {
        if (!state->scanning || state->stop_requested) goto done;
        if (!alive[host]) continue;

        uint32_t ip = subnet_base | host;
        if (ip == own_ip) continue;

        char ip_str[16];
        ip_to_str(ip, ip_str, sizeof(ip_str));

        DiscoveredMiner result;
        ScanError serr = scan_ip_http_only(ip_str, &result);
        if (serr == SCAN_OK) {
            state->results[state->result_count] = result;
            state->result_count++;
            ESP_LOGI(TAG, "Found miner at %s: type=%s model=%s mac=%s",
                     ip_str, miner_type_to_string(result.type), result.model, result.mac);
            if (on_found) on_found(&result, cb_ctx);
        } else {
            switch (serr) {
                case SCAN_ERR_ALLOC:   state->err_alloc++; break;
                case SCAN_ERR_INIT:    state->err_init++; break;
                case SCAN_ERR_PERFORM: state->err_perform++; break;
                case SCAN_ERR_STATUS:  state->err_status++; break;
                case SCAN_ERR_PARSE:   state->err_parse++; break;
                default: break;
            }
        }
    }

    // Phase 3: cgminer probe on alive hosts not found by HTTP
    ESP_LOGI(TAG, "Starting cgminer pass on %d alive non-HTTP IPs...", alive_count - state->result_count);
    for (int host = 1; host <= 254 && state->result_count < SCAN_MAX_RESULTS; host++) {
        if (!state->scanning || state->stop_requested) goto done;
        if (!alive[host]) continue;

        uint32_t ip = subnet_base | host;
        if (ip == own_ip) continue;

        char ip_str[16];
        ip_to_str(ip, ip_str, sizeof(ip_str));

        // Skip IPs already found in HTTP pass
        bool already_found = false;
        for (int i = 0; i < state->result_count; i++) {
            if (strcmp(state->results[i].ip, ip_str) == 0) {
                already_found = true;
                break;
            }
        }
        if (already_found) continue;

        DiscoveredMiner result;
        if (scan_ip_cgminer(ip_str, &result)) {
            state->results[state->result_count] = result;
            state->result_count++;
            ESP_LOGI(TAG, "Found cgminer at %s: model=%s mac=%s",
                     ip_str, result.model, result.mac);
            if (on_found) on_found(&result, cb_ctx);
        }
    }

done:
    if (state->stop_requested) {
        ESP_LOGI(TAG, "Scan aborted by user after %d hosts", state->result_count);
    }
    ESP_LOGI(TAG, "Scan done: found=%d (from %d alive) alloc=%d init=%d perform=%d status=%d parse=%d",
             state->result_count, alive_count,
             state->err_alloc, state->err_init, state->err_perform,
             state->err_status, state->err_parse);
    state->scanning = false;
    state->complete = true;
}
