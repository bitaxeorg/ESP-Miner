/**
 * Miner Adapter — portable miner discovery and polling
 *
 * Discovers and polls ASIC miners on the local network.
 * All I/O goes through gateway_platform.h — no SDK headers here.
 *
 * Platform-specific sections guarded with #ifdef ESP_PLATFORM:
 *   - Socket includes (lwip/sockets.h vs sys/socket.h)
 *   - ARP sweep (uses ESP-IDF lwIP internals; Mac uses sequential probing)
 */

#ifdef ESP_PLATFORM
# include "cJSON.h"
# include "lwip/sockets.h"
# include "lwip/netdb.h"
# include "esp_netif.h"
# include "esp_netif_net_stack.h"
# include "freertos/FreeRTOS.h"
# include "freertos/task.h"
# include "lwip/prot/ethernet.h"
# include "lwip/etharp.h"
# include "lwip/tcpip.h"
#else
# include <cjson/cJSON.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <unistd.h>
# include <fcntl.h>
# include <sys/select.h>
# include <errno.h>
#endif

#include "miner_adapter.h"
#include "gateway_platform.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

static const char *TAG = "miner_adapter";

#define HTTP_RECV_BUF_SIZE    4096
#define CGMINER_PORT          4028
#define CGMINER_RECV_BUF_SIZE 4096

// ============================================================
// Type helpers
// ============================================================

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
    if (strcasecmp(str, "bitaxe")      == 0 || strcasecmp(str, "AxeOs")      == 0) return MINER_TYPE_BITAXE;
    if (strcasecmp(str, "hammer")      == 0 || strcasecmp(str, "Hammer")     == 0) return MINER_TYPE_HAMMER;
    if (strcasecmp(str, "nerd")        == 0 || strcasecmp(str, "Nerd")       == 0) return MINER_TYPE_NERD;
    if (strcasecmp(str, "canaan")      == 0 ||
        strcasecmp(str, "AvalonNano3s")== 0 ||
        strcasecmp(str, "AvalonQ")     == 0 ||
        strcasecmp(str, "AvalonMini3") == 0) return MINER_TYPE_CANAAN;
    return MINER_TYPE_UNKNOWN;
}

const char *miner_type_to_adapter_type(MinerType type)
{
    switch (type) {
    case MINER_TYPE_BITAXE: return "AxeOs";
    case MINER_TYPE_HAMMER: return "Hammer";
    case MINER_TYPE_NERD:   return "Nerd";
    case MINER_TYPE_CANAAN: return "AvalonNano3s"; // default Canaan adapter type
    default:                return "AxeOs";
    }
}

// ============================================================
// cgminer TCP helper — direct POSIX sockets (portable)
// ============================================================

static bool cgminer_command_timeout(const char *ip, const char *cmd,
                                     char *response, int resp_size,
                                     int connect_timeout_ms)
{
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port   = htons(CGMINER_PORT);
    inet_pton(AF_INET, ip, &dest_addr.sin_addr);

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) return false;

    if (connect_timeout_ms > 0) {
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);

        int ret = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (ret != 0 && errno == EINPROGRESS) {
            fd_set fdset;
            FD_ZERO(&fdset);
            FD_SET(sock, &fdset);
            struct timeval tv = {
                .tv_sec  = connect_timeout_ms / 1000,
                .tv_usec = (connect_timeout_ms % 1000) * 1000,
            };
            ret = select(sock + 1, NULL, &fdset, NULL, &tv);
            if (ret <= 0) { close(sock); return false; }
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
            if (so_error != 0) { close(sock); return false; }
        } else if (ret != 0) {
            close(sock); return false;
        }
        fcntl(sock, F_SETFL, flags); // restore blocking
    } else {
        if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
            close(sock); return false;
        }
    }

    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (send(sock, cmd, strlen(cmd), 0) < 0) { close(sock); return false; }

    int total = 0;
    while (total < resp_size - 1) {
        int r = recv(sock, response + total, resp_size - 1 - total, 0);
        if (r <= 0) break;
        total += r;
    }
    response[total] = '\0';
    close(sock);
    return total > 0;
}

static bool cgminer_command(const char *ip, const char *cmd, char *response, int resp_size)
{
    return cgminer_command_timeout(ip, cmd, response, resp_size, 0);
}

// ============================================================
// JSON helpers
// ============================================================

static cJSON *parse_cgminer_response(const char *response)
{
    if (!response || !response[0]) return NULL;
    return cJSON_Parse(response);
}

static MinerType detect_axeos_type(cJSON *root)
{
    cJSON *axeos_ver    = cJSON_GetObjectItem(root, "axeOSVersion");
    cJSON *dev_model_lc = cJSON_GetObjectItem(root, "deviceModel");
    cJSON *dev_model_uc = cJSON_GetObjectItem(root, "DeviceModel");

    if (cJSON_IsString(dev_model_lc) && strstr(dev_model_lc->valuestring, "Nerd"))
        return MINER_TYPE_NERD;
    if (cJSON_IsString(axeos_ver))
        return MINER_TYPE_BITAXE;
    if (cJSON_IsString(dev_model_uc) && !cJSON_IsString(axeos_ver))
        return MINER_TYPE_HAMMER;
    return MINER_TYPE_BITAXE;
}

static double parse_diff_string(const char *str)
{
    if (!str) return 0;
    char *end;
    double val = strtod(str, &end);
    if (end == str) return 0;
    switch (*end) {
    case 'k': case 'K': return val * 1e3;
    case 'M': return val * 1e6;
    case 'G': return val * 1e9;
    case 'T': return val * 1e12;
    case 'P': return val * 1e15;
    default:  return val;
    }
}

static double extract_diff(cJSON *item)
{
    if (cJSON_IsNumber(item)) return item->valuedouble;
    if (cJSON_IsString(item)) return parse_diff_string(item->valuestring);
    return 0;
}

// ============================================================
// mmget — extract value from MM ID0 bracketed format
// ============================================================

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

// ============================================================
// poll_axeos — poll an AxeOS-family miner (Bitaxe / Hammer / Nerd)
// ============================================================

static bool poll_axeos(const char *ip, PeerMinerState *peer, int timeout_ms)
{
    char url[64];
    snprintf(url, sizeof(url), "http://%s/api/system/info", ip);

    char *recv_buf = (char *)platform_malloc_large(HTTP_RECV_BUF_SIZE);
    if (!recv_buf) {
        GW_LOGE(TAG, "alloc failed for peer %s", ip);
        return false;
    }

    int status = platform_http_request(url, "GET", NULL, recv_buf, HTTP_RECV_BUF_SIZE, timeout_ms);
    if (status != 200 || !recv_buf[0]) {
        GW_LOGW(TAG, "peer %s AxeOS poll: HTTP %d", ip, status);
        free(recv_buf);
        return false;
    }

    cJSON *root = cJSON_Parse(recv_buf);
    free(recv_buf);
    if (!root) {
        GW_LOGW(TAG, "peer %s: JSON parse failed", ip);
        return false;
    }

    peer->type = detect_axeos_type(root);

    cJSON *item;
    item = cJSON_GetObjectItem(root, "hashRate");
    if (cJSON_IsNumber(item)) peer->hashrate = (float)item->valuedouble;

    float t1 = 0, t2 = 0;
    item = cJSON_GetObjectItem(root, "temp");
    if (cJSON_IsNumber(item)) { t1 = (float)item->valuedouble; peer->temp = t1; }
    item = cJSON_GetObjectItem(root, "temp2");
    if (cJSON_IsNumber(item) && item->valuedouble > 0) t2 = (float)item->valuedouble;
    if (t1 > 0 && t2 > 0) {
        peer->temp_avg = (t1 + t2) / 2.0f;
        peer->temp_max = t1 > t2 ? t1 : t2;
    } else if (t1 > 0) {
        peer->temp_avg = t1; peer->temp_max = t1;
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

    item = cJSON_GetObjectItem(root, "deviceModel");
    if (!cJSON_IsString(item)) item = cJSON_GetObjectItem(root, "DeviceModel");
    if (cJSON_IsString(item)) strncpy(peer->device_model, item->valuestring, sizeof(peer->device_model) - 1);

    item = cJSON_GetObjectItem(root, "vrTemp");
    if (cJSON_IsNumber(item)) peer->vr_temp = (float)item->valuedouble;
    item = cJSON_GetObjectItem(root, "fanspeed");
    if (cJSON_IsNumber(item)) peer->fan_pct = (int)item->valuedouble;

    {
        int rpm_sum = 0, cnt = 0;
        item = cJSON_GetObjectItem(root, "fanrpm");
        if (cJSON_IsNumber(item) && item->valueint > 0) { rpm_sum += item->valueint; cnt++; }
        item = cJSON_GetObjectItem(root, "fan2rpm");
        if (cJSON_IsNumber(item) && item->valueint > 0) { rpm_sum += item->valueint; cnt++; }
        if (cnt > 0) peer->fan_rpm = rpm_sum / cnt;
    }

    item = cJSON_GetObjectItem(root, "wifiRSSI");
    if (cJSON_IsNumber(item)) peer->wifi_rssi = (int)item->valuedouble;

    peer->best_diff         = extract_diff(cJSON_GetObjectItem(root, "bestDiff"));
    peer->best_session_diff = extract_diff(cJSON_GetObjectItem(root, "bestSessionDiff"));

    item = cJSON_GetObjectItem(root, "poolDifficulty");
    if (cJSON_IsNumber(item)) peer->pool_difficulty = item->valuedouble;

    item = cJSON_GetObjectItem(root, "foundBlocks");
    if (!cJSON_IsNumber(item)) item = cJSON_GetObjectItem(root, "blockFound");
    if (cJSON_IsNumber(item)) peer->found_blocks = (int)item->valuedouble;

    peer->pool_count = 0;
    bool using_fallback = false;
    item = cJSON_GetObjectItem(root, "isUsingFallbackStratum");
    if (cJSON_IsNumber(item)) using_fallback = (item->valuedouble != 0);
    if (cJSON_IsBool(item))   using_fallback = cJSON_IsTrue(item);

    cJSON *surl = cJSON_GetObjectItem(root, "stratumURL");
    cJSON *sport = cJSON_GetObjectItem(root, "stratumPort");
    cJSON *suser = cJSON_GetObjectItem(root, "stratumUser");
    if (cJSON_IsString(surl) && surl->valuestring[0]) {
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
    if (cJSON_IsString(furl) && furl->valuestring[0]) {
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

    peer->last_seen = platform_time_ms();
    cJSON_Delete(root);
    return true;
}

// ============================================================
// poll_canaan — poll a Canaan/Avalon miner via cgminer API
// ============================================================

static bool poll_canaan(const char *ip, PeerMinerState *peer)
{
    char *resp_buf = (char *)platform_malloc_large(CGMINER_RECV_BUF_SIZE);
    if (!resp_buf) {
        GW_LOGE(TAG, "alloc failed for cgminer peer %s", ip);
        return false;
    }

    // Version + MAC
    if (cgminer_command(ip, "{\"command\":\"version\"}", resp_buf, CGMINER_RECV_BUF_SIZE)) {
        cJSON *root = parse_cgminer_response(resp_buf);
        if (root) {
            cJSON *ver_arr = cJSON_GetObjectItem(root, "VERSION");
            if (cJSON_IsArray(ver_arr) && cJSON_GetArraySize(ver_arr) > 0) {
                cJSON *ver = cJSON_GetArrayItem(ver_arr, 0);
                cJSON *prod = cJSON_GetObjectItem(ver, "PROD");
                cJSON *mac  = cJSON_GetObjectItem(ver, "MAC");
                if (cJSON_IsString(prod)) strncpy(peer->asic_model, prod->valuestring, sizeof(peer->asic_model) - 1);
                if (cJSON_IsString(mac))  strncpy(peer->mac_addr,   mac->valuestring,  sizeof(peer->mac_addr) - 1);
            }
            cJSON_Delete(root);
        }
    }

    // Summary (hashrate, temps, power, uptime)
    if (cgminer_command(ip, "{\"command\":\"summary\"}", resp_buf, CGMINER_RECV_BUF_SIZE)) {
        cJSON *root = parse_cgminer_response(resp_buf);
        if (root) {
            cJSON *sum_arr = cJSON_GetObjectItem(root, "SUMMARY");
            if (cJSON_IsArray(sum_arr) && cJSON_GetArraySize(sum_arr) > 0) {
                cJSON *s = cJSON_GetArrayItem(sum_arr, 0);
                cJSON *item;
                item = cJSON_GetObjectItem(s, "MHS 5s");
                if (!cJSON_IsNumber(item)) item = cJSON_GetObjectItem(s, "GHS 5s");
                if (cJSON_IsNumber(item)) peer->hashrate = (float)(item->valuedouble * 1e6);

                item = cJSON_GetObjectItem(s, "Accepted");
                if (cJSON_IsNumber(item)) peer->shares_accepted = (uint64_t)item->valuedouble;
                item = cJSON_GetObjectItem(s, "Rejected");
                if (cJSON_IsNumber(item)) peer->shares_rejected = (uint64_t)item->valuedouble;
                item = cJSON_GetObjectItem(s, "Elapsed");
                if (cJSON_IsNumber(item)) peer->uptime_seconds = (uint32_t)item->valuedouble;
            }
            cJSON_Delete(root);
        }
    }

    // Pools
    if (cgminer_command(ip, "{\"command\":\"pools\"}", resp_buf, CGMINER_RECV_BUF_SIZE)) {
        cJSON *root = parse_cgminer_response(resp_buf);
        if (root) {
            cJSON *parr = cJSON_GetObjectItem(root, "POOLS");
            if (cJSON_IsArray(parr)) {
                peer->pool_count = 0;
                int pcount = cJSON_GetArraySize(parr);
                for (int i = 0; i < pcount && peer->pool_count < 3; i++) {
                    cJSON *pool = cJSON_GetArrayItem(parr, i);
                    PoolInfo *p = &peer->pools[peer->pool_count];
                    memset(p, 0, sizeof(PoolInfo));
                    cJSON *u    = cJSON_GetObjectItem(pool, "URL");
                    cJSON *usr  = cJSON_GetObjectItem(pool, "User");
                    cJSON *stat = cJSON_GetObjectItem(pool, "Status");
                    cJSON *diff = cJSON_GetObjectItem(pool, "Diff");
                    cJSON *acc  = cJSON_GetObjectItem(pool, "Accepted");
                    cJSON *rej  = cJSON_GetObjectItem(pool, "Rejected");
                    if (cJSON_IsString(u))   strncpy(p->url,  u->valuestring,   sizeof(p->url) - 1);
                    if (cJSON_IsString(usr)) strncpy(p->user, usr->valuestring,  sizeof(p->user) - 1);
                    if (cJSON_IsString(stat)) p->active = (strcmp(stat->valuestring, "Alive") == 0);
                    if (cJSON_IsNumber(diff)) p->stratum_diff = diff->valuedouble;
                    if (cJSON_IsNumber(acc))  p->accepted = (uint64_t)acc->valuedouble;
                    if (cJSON_IsNumber(rej))  p->rejected = (uint64_t)rej->valuedouble;
                    peer->pool_count++;
                }
            }
            cJSON_Delete(root);
        }
    }

    // estats (temp, fan, freq from MM ID0)
    if (cgminer_command(ip, "{\"command\":\"estats\"}", resp_buf, CGMINER_RECV_BUF_SIZE)) {
        cJSON *root = parse_cgminer_response(resp_buf);
        if (root) {
            cJSON *sarr = cJSON_GetObjectItem(root, "STATS");
            if (cJSON_IsArray(sarr) && cJSON_GetArraySize(sarr) > 0) {
                cJSON *s = cJSON_GetArrayItem(sarr, 0);
                cJSON *mmid0 = cJSON_GetObjectItem(s, "MM ID0");
                if (!cJSON_IsString(mmid0)) mmid0 = cJSON_GetObjectItem(s, "MM ID0:Summary");
                if (cJSON_IsString(mmid0)) {
                    const char *mm = mmid0->valuestring;
                    char val[64];
                    if (mmget(mm, "TIn",   val, sizeof(val))) peer->temp     = (float)atof(val);
                    if (mmget(mm, "TMax",  val, sizeof(val))) peer->temp_max = (float)atof(val);
                    if (mmget(mm, "TAvg",  val, sizeof(val))) peer->temp_avg = (float)atof(val);
                    if (mmget(mm, "Fan1",  val, sizeof(val))) peer->fan_rpm  = atoi(val);
                    if (mmget(mm, "FanR",  val, sizeof(val))) peer->fan_pct  = atoi(val);
                    if (mmget(mm, "Freq",  val, sizeof(val))) peer->frequency = (float)atof(val);
                    if (mmget(mm, "GHSmm", val, sizeof(val)) && peer->hashrate == 0)
                        peer->hashrate = (float)(atof(val) * 1e9);
                }
            }
            cJSON_Delete(root);
        }
    }

    free(resp_buf);
    peer->last_seen = platform_time_ms();
    peer->type = MINER_TYPE_CANAAN;
    return true; // cgminer connection itself succeeded
}

// ============================================================
// Public poll_miner
// ============================================================

bool poll_miner(const char *ip, MinerType type, PeerMinerState *peer)
{
    switch (type) {
    case MINER_TYPE_CANAAN:
        return poll_canaan(ip, peer);
    default:
        // Bitaxe, Hammer, Nerd, Unknown — all try AxeOS HTTP
        return poll_axeos(ip, peer, 5000);
    }
}

// ============================================================
// scan_ip — probe a single IP for a known miner
// ============================================================

bool scan_ip(const char *ip, DiscoveredMiner *result)
{
    memset(result, 0, sizeof(DiscoveredMiner));
    strncpy(result->ip, ip, sizeof(result->ip) - 1);

    // Try AxeOS HTTP
    {
        char url[64];
        snprintf(url, sizeof(url), "http://%s/api/system/info", ip);
        char *recv_buf = (char *)platform_malloc_large(HTTP_RECV_BUF_SIZE);
        if (recv_buf) {
            int status = platform_http_request(url, "GET", NULL, recv_buf, HTTP_RECV_BUF_SIZE, 500);
            if (status == 200 && recv_buf[0]) {
                cJSON *root = cJSON_Parse(recv_buf);
                free(recv_buf);
                if (root) {
                    result->type     = detect_axeos_type(root);
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
        }
    }

    // Try cgminer on port 4028
    {
        char *cgminer_buf = (char *)platform_malloc_large(CGMINER_RECV_BUF_SIZE);
        if (cgminer_buf) {
            if (cgminer_command_timeout(ip, "{\"command\":\"version\"}", cgminer_buf, CGMINER_RECV_BUF_SIZE, 500)) {
                result->type      = MINER_TYPE_CANAAN;
                result->reachable = true;
                cJSON *root = parse_cgminer_response(cgminer_buf);
                if (root) {
                    cJSON *arr = cJSON_GetObjectItem(root, "VERSION");
                    if (cJSON_IsArray(arr) && cJSON_GetArraySize(arr) > 0) {
                        cJSON *item = cJSON_GetObjectItem(cJSON_GetArrayItem(arr, 0), "Type");
                        if (cJSON_IsString(item))
                            strncpy(result->model, item->valuestring, sizeof(result->model) - 1);
                    }
                    cJSON_Delete(root);
                }
                free(cgminer_buf);
                return true;
            }
            free(cgminer_buf);
        }
    }

    return false;
}

// ============================================================
// IP formatting helper
// ============================================================

static void ip_u32_to_str(uint32_t ip_hostorder, char *buf, int buf_size)
{
    snprintf(buf, buf_size, "%d.%d.%d.%d",
             (int)((ip_hostorder >> 24) & 0xFF),
             (int)((ip_hostorder >> 16) & 0xFF),
             (int)((ip_hostorder >> 8)  & 0xFF),
             (int)( ip_hostorder        & 0xFF));
}

// ============================================================
// run_network_scan
// ============================================================

#ifdef ESP_PLATFORM
// ── ESP32: ARP sweep + targeted HTTP/cgminer probes ──────────────────────

// (ARP sweep code uses lwIP internals and FreeRTOS, intentionally gated)
# include "lwip/prot/ethernet.h"
# include "lwip/etharp.h"
# include "lwip/tcpip.h"

#define ARP_BATCH_SIZE   10
#define ARP_BATCH_WAIT_MS 80

typedef struct {
    struct netif   *netif;
    uint32_t        subnet_base;
    uint32_t        own_ip;
    bool           *alive;
    int            *alive_count;
    int             batch_start;
    int             batch_end;
    volatile bool   done;
} arp_batch_ctx_t;

static void arp_batch_cb(void *arg)
{
    arp_batch_ctx_t *ctx = (arp_batch_ctx_t *)arg;
    for (int h = ctx->batch_start; h <= ctx->batch_end; h++) {
        uint32_t ip_ne = htonl(ctx->subnet_base | h);
        if (ip_ne == htonl(ctx->own_ip)) continue;
        ip4_addr_t ip4 = { .addr = ip_ne };
        etharp_request(ctx->netif, &ip4);
    }
    ctx->done = true;
}

static void arp_harvest_cb(void *arg)
{
    arp_batch_ctx_t *ctx = (arp_batch_ctx_t *)arg;
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        ip4_addr_t *entry_ip;
        struct eth_addr *entry_mac;
        struct netif *entry_netif = NULL;
        int found = etharp_get_entry(i, &entry_ip, &entry_netif, &entry_mac);
        if (!found || !entry_ip || entry_netif != ctx->netif) continue;
        uint32_t entry_host = ntohl(entry_ip->addr);
        int host_octet = (int)(entry_host & 0xFF);
        uint32_t entry_subnet = entry_host & 0xFFFFFF00;
        if (entry_subnet != ctx->subnet_base) continue;
        if (host_octet < 1 || host_octet > 254) continue;
        if (!ctx->alive[host_octet]) {
            ctx->alive[host_octet] = true;
            (*ctx->alive_count)++;
        }
    }
    ctx->done = true;
}

static int arp_sweep(uint32_t subnet_base, uint32_t own_ip,
                     bool alive[255], volatile bool *stop)
{
    esp_netif_t *esp_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!esp_netif) return -1;
    struct netif *lwip_netif = (struct netif *)esp_netif_get_netif_impl(esp_netif);
    if (!lwip_netif) return -1;

    int alive_count = 0;
    arp_batch_ctx_t ctx = {
        .netif       = lwip_netif,
        .subnet_base = subnet_base,
        .own_ip      = own_ip,
        .alive       = alive,
        .alive_count = &alive_count,
        .done        = false,
    };

    for (int bs = 1; bs <= 254; bs += ARP_BATCH_SIZE) {
        if (stop && *stop) break;
        ctx.batch_start = bs;
        ctx.batch_end   = bs + ARP_BATCH_SIZE - 1 > 254 ? 254 : bs + ARP_BATCH_SIZE - 1;
        ctx.done = false;
        tcpip_callback(arp_batch_cb, &ctx);
        while (!ctx.done) vTaskDelay(1);
        vTaskDelay(pdMS_TO_TICKS(ARP_BATCH_WAIT_MS));
        ctx.done = false;
        tcpip_callback(arp_harvest_cb, &ctx);
        while (!ctx.done) vTaskDelay(1);
    }
    return alive_count;
}

void run_network_scan(ScanState *state, scan_found_cb_t on_found, void *cb_ctx)
{
    GW_LOGI(TAG, "Starting scan (ARP + HTTP)...");
    state->result_count = 0;
    state->complete     = false;

    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif || esp_netif_get_ip_info(netif, &ip_info) != ESP_OK || !ip_info.ip.addr) {
        GW_LOGE(TAG, "No network interface"); state->scanning = false; state->complete = true; return;
    }

    uint32_t own_ip      = ntohl(ip_info.ip.addr);
    uint32_t subnet_base = own_ip & 0xFFFFFF00;

    GW_LOGI(TAG, "Scanning %d.%d.%d.0/24",
            (subnet_base >> 24) & 0xFF, (subnet_base >> 16) & 0xFF, (subnet_base >> 8) & 0xFF);

    bool alive[255] = {0};
    int alive_count = arp_sweep(subnet_base, own_ip, alive, &state->stop_requested);
    if (alive_count < 0) {
        GW_LOGW(TAG, "ARP sweep failed, scanning all IPs");
        for (int i = 1; i <= 254; i++) alive[i] = true;
        alive_count = 254;
    }

    if (!state->scanning || state->stop_requested) goto done;

    // HTTP probe
    for (int h = 1; h <= 254 && state->result_count < SCAN_MAX_RESULTS; h++) {
        if (!state->scanning || state->stop_requested) goto done;
        if (!alive[h]) continue;
        uint32_t ip = subnet_base | h;
        if (ip == own_ip) continue;
        char ip_str[16];
        ip_u32_to_str(ip, ip_str, sizeof(ip_str));
        DiscoveredMiner result;
        if (scan_ip(ip_str, &result)) {
            state->results[state->result_count++] = result;
            GW_LOGI(TAG, "Found: %s (%s) mac=%s", ip_str, miner_type_to_string(result.type), result.mac);
            if (on_found) on_found(&result, cb_ctx);
        }
    }

done:
    GW_LOGI(TAG, "Scan done: %d found", state->result_count);
    state->scanning = false;
    state->complete = true;
}

#else
// ── Non-ESP32 (macOS, Linux): sequential HTTP + cgminer probe ────────────

void run_network_scan(ScanState *state, scan_found_cb_t on_found, void *cb_ctx)
{
    GW_LOGI(TAG, "Starting sequential network scan...");
    state->result_count = 0;
    state->complete     = false;

    char local_ip[32] = "";
    platform_get_local_ip(local_ip, sizeof(local_ip));
    if (!local_ip[0]) {
        GW_LOGE(TAG, "Cannot determine local IP"); state->scanning = false; state->complete = true; return;
    }

    // Parse subnet base from local IP
    unsigned int a, b, c, d;
    if (sscanf(local_ip, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        GW_LOGE(TAG, "Invalid local IP: %s", local_ip); state->scanning = false; state->complete = true; return;
    }
    char subnet_prefix[20];
    snprintf(subnet_prefix, sizeof(subnet_prefix), "%u.%u.%u.", a, b, c);

    GW_LOGI(TAG, "Scanning %s0/24 from %s", subnet_prefix, local_ip);

    for (int h = 1; h <= 254 && state->result_count < SCAN_MAX_RESULTS; h++) {
        if (!state->scanning || state->stop_requested) break;

        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), "%s%d", subnet_prefix, h);
        if (strcmp(ip_str, local_ip) == 0) continue;

        DiscoveredMiner result;
        if (scan_ip(ip_str, &result)) {
            state->results[state->result_count++] = result;
            GW_LOGI(TAG, "Found: %s (%s) mac=%s", ip_str, miner_type_to_string(result.type), result.mac);
            if (on_found) on_found(&result, cb_ctx);
        }
    }

    GW_LOGI(TAG, "Scan done: %d found", state->result_count);
    state->scanning = false;
    state->complete = true;
}

#endif // ESP_PLATFORM
