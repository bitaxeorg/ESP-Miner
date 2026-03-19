/**
 * Gateway Core — tRPC WebSocket client (platform-independent)
 *
 * Uses wslink (gateway/include/wslink.h) for all tRPC protocol concerns:
 *   - connectionParams handshake
 *   - subscription / mutation message framing and ID tracking
 *   - PING/PONG keep-alive
 *   - subscription replay on reconnect
 *
 * This file is responsible only for:
 *   - Providing connectionParams credentials to wslink
 *   - Subscribing to client.onCommand and dispatching received commands
 *   - Sending commandAck / commandResult mutations
 */

#ifdef ESP_PLATFORM
# include "cJSON.h"
#else
# include <cjson/cJSON.h>
#endif

#include "gateway_core.h"
#include "gateway_platform.h"
#include "miner_adapter.h"
#include "wslink.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "gateway_core";

/* ── Module state ─────────────────────────────────────────────────────────── */

static GatewayConfig    g_cfg;
static GatewayModule   *g_gw             = NULL;
static wslink_client_t *g_wslink         = NULL;
static volatile bool    g_connected      = false;
static volatile bool    g_authenticated  = false;
static volatile bool    g_scan_stop_requested = false;

bool gateway_core_is_connected(void)     { return g_connected; }
bool gateway_core_is_authenticated(void) { return g_authenticated; }

/* ── wslink config callbacks ──────────────────────────────────────────────── */

/* get_connection_params: builds the data object for the connectionParams
 * message (mirrors WebSocketClientOptions.connectionParams in options.ts). */
static cJSON *build_connection_params(void *user_data)
{
    (void)user_data;
    char mac_str[20] = "";
    platform_get_mac_str(mac_str, sizeof(mac_str));

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "clientId",     g_cfg.client_id);
    cJSON_AddStringToObject(data, "clientSecret", g_cfg.client_secret);
    cJSON_AddStringToObject(data, "firmware",     g_cfg.version);
    if (mac_str[0])
        cJSON_AddStringToObject(data, "mac", mac_str);

    GW_LOGI(TAG, "connectionParams: clientId=%s mac=%s firmware=%s",
            g_cfg.client_id, mac_str, g_cfg.version);
    return data;
}

static void on_wslink_open(void *user_data)
{
    (void)user_data;
    g_connected = true;
    GW_LOGI(TAG, "WS open — connectionParams sent, subscriptions replayed");
}

static void on_wslink_close(void *user_data)
{
    (void)user_data;
    g_connected     = false;
    g_authenticated = false;
    GW_LOGW(TAG, "WS closed");
}

/* ── Called by the platform layer on WS events ────────────────────────────── */

void gateway_core_on_ws_connected(void)
{
    wslink_on_connected(g_wslink);
}

void gateway_core_on_ws_disconnected(void)
{
    wslink_on_disconnected(g_wslink);
}

void gateway_core_on_message(const char *data, int len)
{
    wslink_on_message(g_wslink, data, len);
}

/* ── Mutation helpers (no more manual JSON framing) ───────────────────────── */

static void send_command_ack(const char *command_id)
{
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "commandId", command_id);
    wslink_mutation(g_wslink, "client.commandAck", input, NULL, NULL);
}

static void send_command_result(const char *command_id, bool success,
                                cJSON *result, const char *error, bool partial)
{
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "commandId", command_id);
    cJSON_AddBoolToObject(input, "success", success);
    if (result)  cJSON_AddItemToObject(input, "result", result);
    if (error)   cJSON_AddStringToObject(input, "error", error);
    if (partial) cJSON_AddBoolToObject(input, "partial", true);
    wslink_mutation(g_wslink, "client.commandResult", input, NULL, NULL);
}

/* ── Build POLL_MINERS result array ───────────────────────────────────────── */

static cJSON *build_poll_result_array(GatewayModule *gw)
{
    cJSON *arr = cJSON_CreateArray();
    if (!arr) return NULL;

    for (int i = 0; i < gw->peer_count; i++) {
        PeerMinerState *peer = &gw->peers[i];

        cJSON *m = cJSON_CreateObject();
        cJSON_AddStringToObject(m, "mac", peer->mac_addr);
        cJSON_AddStringToObject(m, "ip",  peer->ip);
        cJSON_AddBoolToObject(m, "online", peer->status == PEER_STATUS_ONLINE);

        if (peer->status == PEER_STATUS_ONLINE) {
            cJSON_AddNumberToObject(m, "hashrate",       peer->hashrate);
            cJSON_AddNumberToObject(m, "temp",           peer->temp);
            cJSON_AddNumberToObject(m, "power",          peer->power);
            cJSON_AddNumberToObject(m, "voltage",        peer->voltage);
            cJSON_AddNumberToObject(m, "current",        peer->current);
            cJSON_AddNumberToObject(m, "sharesAccepted", (double)peer->shares_accepted);
            cJSON_AddNumberToObject(m, "sharesRejected", (double)peer->shares_rejected);
            cJSON_AddNumberToObject(m, "uptimeSeconds",  (double)peer->uptime_seconds);
            cJSON_AddNumberToObject(m, "frequency",      peer->frequency);

            if (peer->vr_temp != 0)          cJSON_AddNumberToObject(m, "vrTemp",          peer->vr_temp);
            if (peer->fan_pct > 0)           cJSON_AddNumberToObject(m, "fanPct",          peer->fan_pct);
            if (peer->fan_rpm > 0)           cJSON_AddNumberToObject(m, "fanRpm",          peer->fan_rpm);
            if (peer->wifi_rssi != 0)        cJSON_AddNumberToObject(m, "wifiRssi",        peer->wifi_rssi);
            if (peer->found_blocks > 0)      cJSON_AddNumberToObject(m, "foundBlocks",     peer->found_blocks);
            if (peer->best_diff > 0)         cJSON_AddNumberToObject(m, "bestDiff",        peer->best_diff);
            if (peer->best_session_diff > 0) cJSON_AddNumberToObject(m, "bestSessionDiff", peer->best_session_diff);
            if (peer->pool_difficulty > 0)   cJSON_AddNumberToObject(m, "poolDiff",        peer->pool_difficulty);
            if (peer->temp_avg > 0)          cJSON_AddNumberToObject(m, "tempAvg",         peer->temp_avg);
            if (peer->temp_max > 0)          cJSON_AddNumberToObject(m, "tempMax",         peer->temp_max);

            if (peer->asic_model[0])   cJSON_AddStringToObject(m, "model",       peer->asic_model);
            if (peer->device_model[0]) cJSON_AddStringToObject(m, "deviceModel", peer->device_model);

            if (peer->pool_count > 0) {
                cJSON *pools = cJSON_CreateArray();
                for (int pi = 0; pi < peer->pool_count; pi++) {
                    PoolInfo *p  = &peer->pools[pi];
                    cJSON    *pj = cJSON_CreateObject();
                    cJSON_AddStringToObject(pj, "url",   p->url);
                    cJSON_AddStringToObject(pj, "user",  p->user);
                    cJSON_AddBoolToObject(pj,   "active", p->active);
                    if (p->stratum_diff > 0) cJSON_AddNumberToObject(pj, "diff",     p->stratum_diff);
                    if (p->accepted > 0)     cJSON_AddNumberToObject(pj, "accepted", (double)p->accepted);
                    if (p->rejected > 0)     cJSON_AddNumberToObject(pj, "rejected", (double)p->rejected);
                    cJSON_AddItemToArray(pools, pj);
                }
                cJSON_AddItemToObject(m, "pools", pools);
            }
        } else if (peer->status == PEER_STATUS_ERROR) {
            cJSON_AddStringToObject(m, "error", "Poll failed");
        }

        cJSON_AddItemToArray(arr, m);
    }
    return arr;
}

/* ── Command handlers ─────────────────────────────────────────────────────── */

static void handle_poll_miners(const char *command_id, cJSON *payload)
{
    if (g_gw->SCAN_STATE.scanning) {
        GW_LOGD(TAG, "Skipping POLL_MINERS during scan");
        return;
    }

    cJSON *miners_arr = cJSON_GetObjectItem(payload, "miners");
    if (!cJSON_IsArray(miners_arr)) return;

    int count = cJSON_GetArraySize(miners_arr);
    if (count > GATEWAY_MAX_PEERS - 1) count = GATEWAY_MAX_PEERS - 1;

    g_gw->peer_count = 0;
    g_gw->is_polling = true;

    for (int i = 0; i < count; i++) {
        cJSON *entry    = cJSON_GetArrayItem(miners_arr, i);
        cJSON *ip_item  = cJSON_GetObjectItem(entry, "ip");
        cJSON *mac_item = cJSON_GetObjectItem(entry, "macAddress");
        cJSON *adp_item = cJSON_GetObjectItem(entry, "adapter");

        if (!cJSON_IsString(ip_item) || !ip_item->valuestring[0]) continue;

        PeerMinerState *peer = &g_gw->peers[g_gw->peer_count];
        memset(peer, 0, sizeof(PeerMinerState));

        strncpy(peer->ip, ip_item->valuestring, GATEWAY_IP_MAX_LEN - 1);
        if (cJSON_IsString(mac_item))
            strncpy(peer->mac_addr, mac_item->valuestring, sizeof(peer->mac_addr) - 1);

        MinerType type = MINER_TYPE_UNKNOWN;
        if (cJSON_IsString(adp_item))
            type = miner_type_from_string(adp_item->valuestring);
        peer->type = type;

        bool ok = poll_miner(peer->ip, peer->type, peer);
        if (ok) {
            peer->status              = PEER_STATUS_ONLINE;
            peer->consecutive_failures = 0;
            peer->last_seen           = platform_time_ms();
        } else {
            peer->consecutive_failures++;
            peer->status = (peer->consecutive_failures >= 3)
                           ? PEER_STATUS_OFFLINE : PEER_STATUS_ERROR;
        }
        g_gw->peer_count++;
    }

    /* Include self-stats if the platform mines */
    if (g_gw->peer_count < GATEWAY_MAX_PEERS) {
        PeerMinerState *self = &g_gw->peers[g_gw->peer_count];
        memset(self, 0, sizeof(PeerMinerState));
        if (platform_get_self_stats(self)) {
            self->last_seen = platform_time_ms();
            g_gw->peer_count++;
        }
    }

    g_gw->is_polling = false;

    if (command_id) {
        cJSON *result = build_poll_result_array(g_gw);
        send_command_result(command_id, true, result, NULL, false);
    }
}

/* Scan streaming context */
typedef struct {
    const char *command_id;
    int         pending_count;
    DiscoveredMiner pending[3];
} scan_stream_ctx_t;

#define SCAN_BATCH_SIZE 3

static void flush_scan_batch(scan_stream_ctx_t *sctx)
{
    if (!sctx->command_id || sctx->pending_count == 0) return;

    for (int i = 0; i < sctx->pending_count; i++) {
        DiscoveredMiner *miner = &sctx->pending[i];

        cJSON *r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "ip",         miner->ip);
        cJSON_AddStringToObject(r, "status",     "MINER");
        cJSON_AddStringToObject(r, "macAddress", miner->mac);
        cJSON_AddStringToObject(r, "adapter",    miner_type_to_adapter_type(miner->type));
        cJSON_AddStringToObject(r, "deviceName", miner->model[0] ? miner->model : miner->hostname);

        send_command_result(sctx->command_id, true, r, NULL, true);
    }
    sctx->pending_count = 0;
}

static void on_scan_miner_found(const DiscoveredMiner *miner, void *ctx)
{
    scan_stream_ctx_t *sctx = (scan_stream_ctx_t *)ctx;
    sctx->pending[sctx->pending_count++] = *miner;
    if (sctx->pending_count >= SCAN_BATCH_SIZE)
        flush_scan_batch(sctx);
}

static void handle_scan_ip_range(const char *command_id, cJSON *payload)
{
    if (g_gw->SCAN_STATE.scanning) {
        GW_LOGW(TAG, "Scan already in progress");
        return;
    }

    if (command_id) send_command_ack(command_id);
    (void)payload;

    GW_LOGI(TAG, "Starting network scan...");
    memset(&g_gw->SCAN_STATE, 0, sizeof(ScanState));
    g_gw->SCAN_STATE.scanning = true;
    g_scan_stop_requested = false;

    scan_stream_ctx_t stream_ctx = { .command_id = command_id, .pending_count = 0 };
    run_network_scan(&g_gw->SCAN_STATE, on_scan_miner_found, &stream_ctx);
    flush_scan_batch(&stream_ctx);

    GW_LOGI(TAG, "Scan complete: %d found", g_gw->SCAN_STATE.result_count);

    if (command_id) {
        cJSON *diag = cJSON_CreateObject();
        cJSON_AddNumberToObject(diag, "totalFound", g_gw->SCAN_STATE.result_count);
        cJSON_AddNumberToObject(diag, "errAlloc",   g_gw->SCAN_STATE.err_alloc);
        cJSON_AddNumberToObject(diag, "errInit",    g_gw->SCAN_STATE.err_init);
        cJSON_AddNumberToObject(diag, "errPerform", g_gw->SCAN_STATE.err_perform);
        cJSON_AddNumberToObject(diag, "errStatus",  g_gw->SCAN_STATE.err_status);
        cJSON_AddNumberToObject(diag, "errParse",   g_gw->SCAN_STATE.err_parse);
        cJSON_AddNumberToObject(diag, "freeHeap",   (double)platform_get_free_heap());
        send_command_result(command_id, true, diag, NULL, false);
    }
}

static void handle_stop_scan(const char *command_id, cJSON *payload)
{
    g_scan_stop_requested             = true;
    g_gw->SCAN_STATE.stop_requested   = true;
    g_gw->SCAN_STATE.scanning         = false;

    if (command_id) send_command_ack(command_id);
    GW_LOGI(TAG, "Scan stop requested");
    (void)payload;
}

/* ── AxeOS flat config helper ─────────────────────────────────────────────── */

static cJSON *flatten_axeos_config(cJSON *config)
{
    cJSON *flat    = cJSON_CreateObject();
    cJSON *perf    = cJSON_GetObjectItem(config, "performance");
    cJSON *thermal = cJSON_GetObjectItem(config, "thermal");
    cJSON *pools   = cJSON_GetObjectItem(config, "pools");
    cJSON *raw     = cJSON_GetObjectItem(config, "raw");

    if (cJSON_IsObject(perf)) {
        cJSON *v;
        if ((v = cJSON_GetObjectItem(perf, "frequency"))   && cJSON_IsNumber(v))
            cJSON_AddNumberToObject(flat, "frequency",   v->valuedouble);
        if ((v = cJSON_GetObjectItem(perf, "coreVoltage")) && cJSON_IsNumber(v))
            cJSON_AddNumberToObject(flat, "coreVoltage", v->valuedouble);
        if ((v = cJSON_GetObjectItem(perf, "powerLimit"))  && cJSON_IsNumber(v))
            cJSON_AddNumberToObject(flat, "powerLimit",  v->valuedouble);
    }

    if (cJSON_IsObject(thermal)) {
        cJSON *v;
        if ((v = cJSON_GetObjectItem(thermal, "autoFanSpeed"))   && cJSON_IsBool(v))
            cJSON_AddBoolToObject(flat, "autofanspeed",   cJSON_IsTrue(v));
        if ((v = cJSON_GetObjectItem(thermal, "minFanSpeed"))    && cJSON_IsNumber(v))
            cJSON_AddNumberToObject(flat, "minFanSpeed",  v->valuedouble);
        if ((v = cJSON_GetObjectItem(thermal, "manualFanSpeed")) && cJSON_IsNumber(v))
            cJSON_AddNumberToObject(flat, "manualFanSpeed", v->valuedouble);
        if ((v = cJSON_GetObjectItem(thermal, "targetTemp"))     && cJSON_IsNumber(v))
            cJSON_AddNumberToObject(flat, "temptarget",   v->valuedouble);
    }

    if (cJSON_IsArray(pools) && cJSON_GetArraySize(pools) > 0) {
        cJSON *p0    = cJSON_GetArrayItem(pools, 0);
        cJSON *url0  = cJSON_GetObjectItem(p0, "url");
        cJSON *user0 = cJSON_GetObjectItem(p0, "username");
        cJSON *pass0 = cJSON_GetObjectItem(p0, "password");

        if (cJSON_IsString(url0)) {
            const char *raw_url    = url0->valuestring;
            const char *host_start = strstr(raw_url, "://");
            host_start = host_start ? host_start + 3 : raw_url;
            const char *colon = strrchr(host_start, ':');
            if (colon) {
                char host[128] = "";
                int  port = atoi(colon + 1);
                int  hlen = (int)(colon - host_start);
                if (hlen > 0 && hlen < (int)sizeof(host)) {
                    memcpy(host, host_start, hlen);
                    host[hlen] = '\0';
                    cJSON_AddStringToObject(flat, "stratumURL",  host);
                    cJSON_AddNumberToObject(flat, "stratumPort", port);
                }
            } else {
                cJSON_AddStringToObject(flat, "stratumURL", host_start);
            }
        }
        if (cJSON_IsString(user0)) cJSON_AddStringToObject(flat, "stratumUser",     user0->valuestring);
        if (cJSON_IsString(pass0)) cJSON_AddStringToObject(flat, "stratumPassword", pass0->valuestring);

        if (cJSON_GetArraySize(pools) > 1) {
            cJSON *p1    = cJSON_GetArrayItem(pools, 1);
            cJSON *url1  = cJSON_GetObjectItem(p1, "url");
            cJSON *user1 = cJSON_GetObjectItem(p1, "username");
            cJSON *pass1 = cJSON_GetObjectItem(p1, "password");
            if (cJSON_IsString(url1)) {
                const char *raw_url    = url1->valuestring;
                const char *host_start = strstr(raw_url, "://");
                host_start = host_start ? host_start + 3 : raw_url;
                const char *colon = strrchr(host_start, ':');
                if (colon) {
                    char host[128] = "";
                    int  port = atoi(colon + 1);
                    int  hlen = (int)(colon - host_start);
                    if (hlen > 0 && hlen < (int)sizeof(host)) {
                        memcpy(host, host_start, hlen);
                        host[hlen] = '\0';
                        cJSON_AddStringToObject(flat, "fallbackStratumURL",  host);
                        cJSON_AddNumberToObject(flat, "fallbackStratumPort", port);
                    }
                }
            }
            if (cJSON_IsString(user1)) cJSON_AddStringToObject(flat, "fallbackStratumUser",     user1->valuestring);
            if (cJSON_IsString(pass1)) cJSON_AddStringToObject(flat, "fallbackStratumPassword", pass1->valuestring);
        }
    }

    if (cJSON_IsObject(raw)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, raw) {
            cJSON_AddItemToObject(flat, item->string, cJSON_Duplicate(item, true));
        }
    }

    return flat;
}

static void handle_miner_command(const char *command_id, const char *cmd_type, cJSON *payload)
{
    cJSON      *mac_item   = cJSON_GetObjectItem(payload, "macAddress");
    const char *target_mac = cJSON_IsString(mac_item) ? mac_item->valuestring : "";

    const char *target_ip = NULL;
    for (int i = 0; i < g_gw->peer_count; i++) {
        if (strcasecmp(g_gw->peers[i].mac_addr, target_mac) == 0) {
            target_ip = g_gw->peers[i].ip;
            break;
        }
    }

    if (!target_ip || !target_ip[0]) {
        if (command_id)
            send_command_result(command_id, false, NULL, "No IP for target miner", false);
        return;
    }

    if (command_id) send_command_ack(command_id);

    MinerType target_type = MINER_TYPE_UNKNOWN;
    for (int i = 0; i < g_gw->peer_count; i++) {
        if (strcasecmp(g_gw->peers[i].mac_addr, target_mac) == 0 ||
            strcmp(g_gw->peers[i].ip, target_ip) == 0) {
            target_type = g_gw->peers[i].type;
            break;
        }
    }
    if (target_type == MINER_TYPE_UNKNOWN) {
        cJSON *adp = cJSON_GetObjectItem(payload, "adapter");
        if (cJSON_IsString(adp))
            target_type = miner_type_from_string(adp->valuestring);
    }

    /* ── Canaan / Avalon: cgminer TCP protocol ──────────────────────────── */
    if (target_type == MINER_TYPE_CANAAN) {
        char *resp_buf = (char *)platform_malloc_large(4096);
        if (!resp_buf) {
            if (command_id)
                send_command_result(command_id, false, NULL, "Out of memory", false);
            return;
        }

        if (strcmp(cmd_type, "RESTART") == 0) {
            platform_tcp_send_recv(target_ip, 4028,
                "{\"command\":\"ascset\",\"parameter\":\"0,reboot,0\"}",
                resp_buf, 4096, 5000);
            if (command_id) send_command_result(command_id, true, NULL, NULL, false);

        } else if (strcmp(cmd_type, "IDENTIFY") == 0) {
            platform_tcp_send_recv(target_ip, 4028,
                "{\"command\":\"ascidentify\",\"parameter\":\"0\"}",
                resp_buf, 4096, 5000);
            if (command_id) send_command_result(command_id, true, NULL, NULL, false);

        } else if (strcmp(cmd_type, "GET_CONFIG") == 0) {
            cJSON *config_result = cJSON_CreateObject();
            cJSON_AddBoolToObject(config_result, "cgminer", true);

            if (platform_tcp_send_recv(target_ip, 4028, "{\"command\":\"version\"}", resp_buf, 4096, 5000)) {
                cJSON *rv = cJSON_Parse(resp_buf);
                if (rv) {
                    cJSON *arr = cJSON_GetObjectItem(rv, "VERSION");
                    if (cJSON_IsArray(arr) && cJSON_GetArraySize(arr) > 0) {
                        cJSON *prod = cJSON_GetObjectItem(cJSON_GetArrayItem(arr, 0), "PROD");
                        if (cJSON_IsString(prod))
                            cJSON_AddStringToObject(config_result, "model", prod->valuestring);
                    }
                    cJSON_Delete(rv);
                }
            }
            if (platform_tcp_send_recv(target_ip, 4028, "{\"command\":\"pools\"}", resp_buf, 4096, 5000)) {
                cJSON *rp = cJSON_Parse(resp_buf);
                if (rp) {
                    cJSON *parr = cJSON_GetObjectItem(rp, "POOLS");
                    if (cJSON_IsArray(parr)) {
                        cJSON *out_pools = cJSON_CreateArray();
                        cJSON *pool;
                        cJSON_ArrayForEach(pool, parr) {
                            cJSON *po  = cJSON_CreateObject();
                            cJSON *u   = cJSON_GetObjectItem(pool, "URL");
                            cJSON *usr = cJSON_GetObjectItem(pool, "User");
                            if (cJSON_IsString(u))   cJSON_AddStringToObject(po, "url",      u->valuestring);
                            if (cJSON_IsString(usr)) cJSON_AddStringToObject(po, "username", usr->valuestring);
                            cJSON_AddItemToArray(out_pools, po);
                        }
                        cJSON_AddItemToObject(config_result, "pools", out_pools);
                    }
                    cJSON_Delete(rp);
                }
            }
            if (platform_tcp_send_recv(target_ip, 4028, "{\"command\":\"estats\"}", resp_buf, 4096, 5000)) {
                cJSON *re = cJSON_Parse(resp_buf);
                if (re) {
                    cJSON *sarr = cJSON_GetObjectItem(re, "STATS");
                    if (cJSON_IsArray(sarr) && cJSON_GetArraySize(sarr) > 0) {
                        cJSON *stats = cJSON_GetArrayItem(sarr, 0);
                        cJSON *mmid0 = cJSON_GetObjectItem(stats, "MM ID0");
                        if (!cJSON_IsString(mmid0))
                            mmid0 = cJSON_GetObjectItem(stats, "MM ID0:Summary");
                        if (cJSON_IsString(mmid0)) {
                            char val[64];
                            if (mmget(mmid0->valuestring, "Freq",      val, sizeof(val))) cJSON_AddNumberToObject(config_result, "frequency",  atof(val));
                            if (mmget(mmid0->valuestring, "WORKMODE",  val, sizeof(val))) cJSON_AddNumberToObject(config_result, "workMode",   atoi(val));
                            if (mmget(mmid0->valuestring, "WORKLEVEL", val, sizeof(val))) cJSON_AddNumberToObject(config_result, "workLevel",  atoi(val));
                            if (mmget(mmid0->valuestring, "MPO",       val, sizeof(val))) cJSON_AddNumberToObject(config_result, "powerLimit", atof(val));
                            if (mmget(mmid0->valuestring, "TarT",      val, sizeof(val))) cJSON_AddNumberToObject(config_result, "targetTemp", atof(val));
                            if (mmget(mmid0->valuestring, "FanR",      val, sizeof(val))) cJSON_AddNumberToObject(config_result, "fanSpeed",   atoi(val));
                        }
                    }
                    cJSON_Delete(re);
                }
            }
            if (command_id) send_command_result(command_id, true, config_result, NULL, false);

        } else if (strcmp(cmd_type, "SET_CONFIG") == 0) {
            cJSON *config      = cJSON_GetObjectItem(payload, "config");
            bool  needs_reboot = false;

            if (cJSON_IsObject(config)) {
                cJSON *perf    = cJSON_GetObjectItem(config, "performance");
                cJSON *thermal = cJSON_GetObjectItem(config, "thermal");
                cJSON *pools   = cJSON_GetObjectItem(config, "pools");

                if (cJSON_IsObject(perf)) {
                    cJSON *wm = cJSON_GetObjectItem(perf, "workMode");
                    if (cJSON_IsNumber(wm)) {
                        char cmd[128];
                        snprintf(cmd, sizeof(cmd), "{\"command\":\"ascset\",\"parameter\":\"0,workmode,set,%d\"}", wm->valueint);
                        platform_tcp_send_recv(target_ip, 4028, cmd, resp_buf, 4096, 5000);
                        needs_reboot = true;
                    }
                    cJSON *wl = cJSON_GetObjectItem(perf, "workLevel");
                    if (cJSON_IsNumber(wl)) {
                        char cmd[128];
                        snprintf(cmd, sizeof(cmd), "{\"command\":\"ascset\",\"parameter\":\"0,worklevel,set,%d\"}", wl->valueint);
                        platform_tcp_send_recv(target_ip, 4028, cmd, resp_buf, 4096, 5000);
                        needs_reboot = true;
                    }
                }
                if (cJSON_IsObject(thermal)) {
                    cJSON *fs = cJSON_GetObjectItem(thermal, "minFanSpeed");
                    if (cJSON_IsNumber(fs)) {
                        char cmd[128];
                        snprintf(cmd, sizeof(cmd), "{\"command\":\"ascset\",\"parameter\":\"0,fan-spd,%d\"}", fs->valueint);
                        platform_tcp_send_recv(target_ip, 4028, cmd, resp_buf, 4096, 5000);
                    }
                }
                if (cJSON_IsArray(pools)) {
                    int pcount = cJSON_GetArraySize(pools);
                    for (int p = 0; p < pcount; p++) {
                        cJSON *pool = cJSON_GetArrayItem(pools, p);
                        cJSON *url  = cJSON_GetObjectItem(pool, "url");
                        cJSON *user = cJSON_GetObjectItem(pool, "username");
                        cJSON *pass = cJSON_GetObjectItem(pool, "password");
                        if (cJSON_IsString(url) && cJSON_IsString(user)) {
                            char cmd[512];
                            snprintf(cmd, sizeof(cmd),
                                "{\"command\":\"setpool\",\"parameter\":\"admin,admin,%d,%s,%s,%s\"}",
                                p, url->valuestring, user->valuestring,
                                cJSON_IsString(pass) ? pass->valuestring : "");
                            platform_tcp_send_recv(target_ip, 4028, cmd, resp_buf, 4096, 5000);
                        }
                    }
                    needs_reboot = true;
                }
                if (needs_reboot) {
                    platform_delay_ms(500);
                    platform_tcp_send_recv(target_ip, 4028,
                        "{\"command\":\"ascset\",\"parameter\":\"0,reboot,0\"}",
                        resp_buf, 4096, 5000);
                }
            }
            if (command_id) send_command_result(command_id, true, NULL, NULL, false);

        } else {
            GW_LOGW(TAG, "Unsupported Canaan command: %s", cmd_type);
            if (command_id)
                send_command_result(command_id, false, NULL, "Unsupported command for Canaan miner", false);
        }

        free(resp_buf);
        return;
    }

    /* ── AxeOS / Hammer / Nerd: HTTP ────────────────────────────────────── */
    char        url[128];
    const char *method    = "POST";
    char       *post_data = NULL;

    if (strcmp(cmd_type, "RESTART") == 0) {
        snprintf(url, sizeof(url), "http://%s/api/system/restart", target_ip);
    } else if (strcmp(cmd_type, "IDENTIFY") == 0) {
        snprintf(url, sizeof(url), "http://%s/api/system/identify", target_ip);
    } else if (strcmp(cmd_type, "GET_CONFIG") == 0) {
        snprintf(url, sizeof(url), "http://%s/api/system/info", target_ip);
        method = "GET";
    } else if (strcmp(cmd_type, "SET_CONFIG") == 0) {
        snprintf(url, sizeof(url), "http://%s/api/system", target_ip);
        method = "PATCH";
        cJSON *config = cJSON_GetObjectItem(payload, "config");
        if (cJSON_IsObject(config)) {
            cJSON *flat = flatten_axeos_config(config);
            post_data = cJSON_PrintUnformatted(flat);
            cJSON_Delete(flat);
        }
    } else {
        GW_LOGW(TAG, "Unknown command type: %s", cmd_type);
        if (command_id)
            send_command_result(command_id, false, NULL, "Unknown command", false);
        return;
    }

    char *recv_buf = (char *)platform_malloc_large(4096);
    if (!recv_buf) {
        if (post_data) free(post_data);
        return;
    }

    int  status  = platform_http_request(url, method, post_data, recv_buf, 4096, 10000);
    bool success = (status >= 200 && status < 300);
    if (post_data) free(post_data);

    if (command_id) {
        if (!success) {
            char err_msg[64];
            snprintf(err_msg, sizeof(err_msg), "HTTP %d", status);
            send_command_result(command_id, false, NULL, err_msg, false);
        } else {
            cJSON *resp_json = recv_buf[0] ? cJSON_Parse(recv_buf) : NULL;
            send_command_result(command_id, true, resp_json, NULL, false);
        }
    }

    free(recv_buf);
    GW_LOGI(TAG, "%s -> %s: %s (HTTP %d)", cmd_type, target_ip, success ? "OK" : "FAIL", status);
}

/* ── onCommand subscription callback ─────────────────────────────────────── */

static void on_command_event(wslink_result_type_t type, cJSON *data,
                             const char *error, void *user_data)
{
    (void)user_data;

    switch (type) {
    case WSLINK_RESULT_STARTED:
        g_authenticated = true;
        GW_LOGI(TAG, "Authenticated — onCommand subscription active");
        break;

    case WSLINK_RESULT_DATA:
        if (!cJSON_IsObject(data)) break;
        {
            cJSON      *id_item   = cJSON_GetObjectItem(data, "id");
            cJSON      *type_item = cJSON_GetObjectItem(data, "type");
            cJSON      *payload   = cJSON_GetObjectItem(data, "payload");
            const char *command_id = cJSON_IsString(id_item)   ? id_item->valuestring   : NULL;
            const char *cmd_type   = cJSON_IsString(type_item) ? type_item->valuestring : "";

            GW_LOGI(TAG, "Command: %s (id=%s)", cmd_type, command_id ? command_id : "?");

            if (!cJSON_IsObject(payload)) payload = data;   /* fallback */

            if (strcmp(cmd_type, "POLL_MINERS") == 0) {
                handle_poll_miners(command_id, payload);
            } else if (strcmp(cmd_type, "SCAN_IP_RANGE") == 0) {
                handle_scan_ip_range(command_id, payload);
            } else if (strcmp(cmd_type, "STOP_SCAN") == 0) {
                handle_stop_scan(command_id, payload);
            } else if (strcmp(cmd_type, "RESTART")    == 0 ||
                       strcmp(cmd_type, "GET_CONFIG") == 0 ||
                       strcmp(cmd_type, "SET_CONFIG") == 0 ||
                       strcmp(cmd_type, "IDENTIFY")   == 0 ||
                       strcmp(cmd_type, "GET_STATS")  == 0) {
                handle_miner_command(command_id, cmd_type, payload);
            } else {
                GW_LOGD(TAG, "Ignoring command type: %s", cmd_type);
            }
        }
        break;

    case WSLINK_RESULT_STOPPED:
        g_authenticated = false;
        GW_LOGW(TAG, "onCommand subscription stopped by server");
        break;

    case WSLINK_RESULT_ERROR:
        g_authenticated = false;
        GW_LOGE(TAG, "onCommand subscription error: %s", error ? error : "unknown");
        break;
    }
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void gateway_core_init(const GatewayConfig *cfg, GatewayModule *gw_module)
{
    g_cfg           = *cfg;
    g_gw            = gw_module;
    g_connected     = false;
    g_authenticated = false;

    wslink_config_t wslink_cfg = {
        .get_connection_params = build_connection_params,
        .on_open               = on_wslink_open,
        .on_close              = on_wslink_close,
        .user_data             = NULL,
    };

    if (g_wslink) wslink_destroy(g_wslink);
    g_wslink = wslink_create(&wslink_cfg);

    /* Register the onCommand subscription once; wslink replays it on every
     * reconnect, so we never need to call this again. */
    wslink_subscribe(g_wslink, "client.onCommand", on_command_event, NULL);
}

void gateway_core_run(void)
{
    if (!g_wslink) {
        GW_LOGE(TAG, "gateway_core_init() not called");
        return;
    }

    GW_LOGI(TAG, "Starting gateway core (url=%s)", g_cfg.url);

    int connect_attempts = 0;

    while (1) {
        size_t free_heap = platform_get_free_heap();
        if (free_heap > 0 && free_heap < 30000) {
            GW_LOGE(TAG, "Heap critically low (%u bytes), rebooting", (unsigned)free_heap);
            platform_reboot();
        }

        connect_attempts++;
        GW_LOGI(TAG, "Connecting (attempt %d, heap=%u)", connect_attempts, (unsigned)free_heap);

        /* wslink_prepare_url mirrors prepareUrl() from utils.ts:
         * appends ?connectionParams=1 when the client has connection params. */
        char connect_url[512];
        wslink_prepare_url(g_wslink, g_cfg.url, connect_url, sizeof(connect_url));
        GW_LOGI(TAG, "Connecting to: %s", connect_url);

        platform_ws_connect(connect_url);

        /* Wait up to 15 s for the transport to connect */
        for (int i = 0; i < 30 && !platform_ws_is_connected(); i++)
            platform_delay_ms(500);

        if (!platform_ws_is_connected()) {
            GW_LOGW(TAG, "Connect timeout (attempt %d)", connect_attempts);
            platform_ws_disconnect();
            platform_delay_ms(5000);
            continue;
        }

        connect_attempts = 0;

        /* Main service loop — platform_ws_service() drains the message queue
         * and calls gateway_core_on_message() → wslink_on_message() for each
         * incoming frame. */
        int tick = 0;
        while (platform_ws_is_connected()) {
            platform_ws_service();
            platform_delay_ms(100);

            if (++tick >= 300) {   /* every 30 s */
                GW_LOGI(TAG, "Connected (auth=%s, heap=%u)",
                        g_authenticated ? "yes" : "pending",
                        (unsigned)platform_get_free_heap());

                tick = 0;
            }
        }

        GW_LOGW(TAG, "Connection lost — reconnecting in 5 s");
        platform_ws_disconnect();
        platform_delay_ms(5000);
    }
}
