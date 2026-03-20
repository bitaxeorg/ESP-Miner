/**
 * Gateway Core — tRPC WebSocket client (platform-independent)
 *
 * The ESP32 is a dumb network proxy.  Exactly three command types are handled:
 *
 *   SCAN_IP_RANGE — TCP-probe requested ports on every host in a CIDR range.
 *                   Reads a "ports" array from the payload (default [80,4028]).
 *                   Sends one partial commandResult per alive host:
 *                     { ip, ports: { "80": bool, "4028": bool } }
 *                   Then sends a final (non-partial) result with diagnostics.
 *
 *   HTTP_REQUEST  — Execute one HTTP call.
 *                   Result: { status: <code>, body: "<response>" }
 *
 *   TCP_REQUEST   — Send one TCP message, read one response.
 *                   Result: { body: "<response>" }
 *
 * All adapter logic, polling, and config parsing lives on the server.
 */

#ifdef ESP_PLATFORM
# include "cJSON.h"
#else
# include <cjson/cJSON.h>
#endif

#include "gateway_core.h"
#include "gateway_platform.h"
#include "wslink.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

static const char *TAG = "gateway_core";

/* ── Module state ─────────────────────────────────────────────────────────── */

static GatewayConfig    g_cfg;
static GatewayModule   *g_gw            = NULL;
static wslink_client_t *g_wslink        = NULL;
static volatile bool    g_connected     = false;
static volatile bool    g_authenticated = false;

bool gateway_core_is_connected(void)     { return g_connected; }
bool gateway_core_is_authenticated(void) { return g_authenticated; }

/* ── wslink config callbacks ──────────────────────────────────────────────── */

static cJSON *build_connection_params(void *user_data)
{
    (void)user_data;
    char mac_str[20] = "";
    char local_ip[16] = "";
    platform_get_mac_str(mac_str, sizeof(mac_str));
    platform_get_local_ip(local_ip, sizeof(local_ip));

    // tRPC connectionParams must be a flat Record<string, string>
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "clientId",     g_cfg.client_id);
    cJSON_AddStringToObject(data, "clientSecret", g_cfg.client_secret);
    cJSON_AddStringToObject(data, "firmware",     g_cfg.version);
    cJSON_AddStringToObject(data, "mac",          mac_str);
    cJSON_AddStringToObject(data, "localIp",      local_ip);

    GW_LOGI(TAG, "connectionParams: clientId=%s mac=%s localIp=%s firmware=%s",
            g_cfg.client_id, mac_str, local_ip, g_cfg.version);
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

/* ── Mutation helper ──────────────────────────────────────────────────────── */

static void send_command_result(const char *command_id, bool success,
                                cJSON *result, const char *error, bool partial)
{
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "commandId", command_id);
    cJSON_AddBoolToObject(input,   "success",   success);
    if (result)  cJSON_AddItemToObject(input, "result",  result);
    if (error)   cJSON_AddStringToObject(input, "error", error);
    if (partial) cJSON_AddBoolToObject(input, "partial", true);
    wslink_mutation(g_wslink, "client.commandResult", input, NULL, NULL);
}

/* ── CIDR helpers ─────────────────────────────────────────────────────────── */

static bool parse_ipv4(const char *str, uint32_t *out)
{
    unsigned a, b, c, d;
    if (sscanf(str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return false;
    if (a > 255 || b > 255 || c > 255 || d > 255)        return false;
    *out = ((a & 0xFFu) << 24) | ((b & 0xFFu) << 16) |
           ((c & 0xFFu) <<  8) |  (d & 0xFFu);
    return true;
}

static void ip_to_str(uint32_t ip, char *buf, int buf_size)
{
    snprintf(buf, buf_size, "%u.%u.%u.%u",
             (unsigned)((ip >> 24) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
             (unsigned)((ip >>  8) & 0xFF), (unsigned)( ip        & 0xFF));
}

/* ── Command handlers ─────────────────────────────────────────────────────── */

/* SCAN_IP_RANGE — TCP probe requested ports per host, stream partials. */
static void handle_scan_ip_range(const char *command_id, cJSON *payload)
{
    if (g_gw->scan_state.scanning) {
        GW_LOGW(TAG, "Scan already in progress");
        return;
    }
    g_gw->scan_state.scanning       = true;
    g_gw->scan_state.stop_requested = false;

    /* Parse ports array from payload; default to [80, 4028]. */
    cJSON *ports_item = cJSON_GetObjectItem(payload, "ports");
    int port_list[16];
    int port_count = 0;
    if (cJSON_IsArray(ports_item)) {
        int n = cJSON_GetArraySize(ports_item);
        if (n > 16) n = 16;
        for (int pi = 0; pi < n; pi++) {
            cJSON *p = cJSON_GetArrayItem(ports_item, pi);
            if (cJSON_IsNumber(p))
                port_list[port_count++] = (int)p->valuedouble;
        }
    }
    if (port_count == 0) {
        port_list[0] = 80;
        port_list[1] = 4028;
        port_count   = 2;
    }

    /* Resolve CIDR: use payload field or fall back to local LAN. */
    char cidr_buf[32] = "";
    cJSON *cidr_item = cJSON_GetObjectItem(payload, "cidr");
    if (cJSON_IsString(cidr_item) && cidr_item->valuestring[0]) {
        strncpy(cidr_buf, cidr_item->valuestring, sizeof(cidr_buf) - 1);
    } else {
        char local_ip[16] = "", netmask[16] = "";
        platform_get_local_ip(local_ip, sizeof(local_ip));
        platform_get_netmask(netmask, sizeof(netmask));

        uint32_t mask = 0, ip4 = 0;
        parse_ipv4(netmask, &mask);
        parse_ipv4(local_ip, &ip4);

        int prefix = 0;
        uint32_t m = mask;
        while (m & 0x80000000u) { prefix++; m <<= 1; }

        uint32_t net = ip4 & mask;
        ip_to_str(net, cidr_buf, sizeof(cidr_buf));
        int len = (int)strlen(cidr_buf);
        snprintf(cidr_buf + len, sizeof(cidr_buf) - len, "/%d", prefix);
    }

    GW_LOGI(TAG, "Starting scan: %s (%d ports)", cidr_buf, port_count);

    /* Parse CIDR */
    uint32_t base_ip = 0;
    int prefix = 24;
    char ip_part[16] = "";
    const char *slash = strchr(cidr_buf, '/');
    if (slash) {
        int hlen = (int)(slash - cidr_buf);
        if (hlen > 0 && hlen < (int)sizeof(ip_part)) {
            memcpy(ip_part, cidr_buf, hlen);
            ip_part[hlen] = '\0';
        }
        prefix = atoi(slash + 1);
    } else {
        strncpy(ip_part, cidr_buf, sizeof(ip_part) - 1);
    }
    parse_ipv4(ip_part, &base_ip);

    if (prefix < 16 || prefix > 30) prefix = 24;  /* safety clamp */
    uint32_t host_bits = (uint32_t)(32 - prefix);
    uint32_t num_hosts = (1u << host_bits) - 2u;   /* exclude net + broadcast */
    uint32_t net_addr  = base_ip & ~((1u << host_bits) - 1u);

    int found = 0;
    char ip_str[16];
    char port_key[8];

    for (uint32_t i = 1; i <= num_hosts; i++) {
        if (g_gw->scan_state.stop_requested) break;

        ip_to_str(net_addr + i, ip_str, sizeof(ip_str));

        bool results[16];
        bool any_open = false;
        for (int pi = 0; pi < port_count; pi++) {
            if (g_gw->scan_state.stop_requested) {
                results[pi] = false;
            } else {
                results[pi] = platform_tcp_probe(ip_str, port_list[pi], 20);
                if (results[pi]) any_open = true;
            }
        }

        if (any_open) {
            cJSON *r     = cJSON_CreateObject();
            cJSON *ports = cJSON_CreateObject();
            cJSON_AddStringToObject(r, "ip", ip_str);
            for (int pi = 0; pi < port_count; pi++) {
                snprintf(port_key, sizeof(port_key), "%d", port_list[pi]);
                cJSON_AddBoolToObject(ports, port_key, results[pi]);
            }
            cJSON_AddItemToObject(r, "ports", ports);
            send_command_result(command_id, true, r, NULL, true);
            found++;
            GW_LOGI(TAG, "Probe %s: alive", ip_str);
        }
    }

    g_gw->scan_state.scanning = false;
    GW_LOGI(TAG, "Scan done: %d hosts (of %" PRIu32 ")", found, num_hosts);

    cJSON *diag = cJSON_CreateObject();
    cJSON_AddNumberToObject(diag, "totalFound", found);
    cJSON_AddNumberToObject(diag, "freeHeap",   (double)platform_get_free_heap());
    send_command_result(command_id, true, diag, NULL, false);
}

/* HTTP_REQUEST — one HTTP call, result: { status, body }. */
static void handle_http_request(const char *command_id, cJSON *payload)
{
    cJSON *ip_item      = cJSON_GetObjectItem(payload, "ip");
    cJSON *port_item    = cJSON_GetObjectItem(payload, "port");
    cJSON *method_item  = cJSON_GetObjectItem(payload, "method");
    cJSON *path_item    = cJSON_GetObjectItem(payload, "path");
    cJSON *body_item    = cJSON_GetObjectItem(payload, "body");
    cJSON *timeout_item = cJSON_GetObjectItem(payload, "timeoutMs");

    const char *ip        = cJSON_IsString(ip_item)     ? ip_item->valuestring    : "";
    int         port      = cJSON_IsNumber(port_item)   ? (int)port_item->valuedouble : 80;
    const char *method    = cJSON_IsString(method_item) ? method_item->valuestring : "GET";
    const char *path      = cJSON_IsString(path_item)   ? path_item->valuestring  : "/";
    const char *body      = cJSON_IsString(body_item)   ? body_item->valuestring  : NULL;
    int         timeout_ms = cJSON_IsNumber(timeout_item) ? (int)timeout_item->valuedouble : 10000;

    if (!ip[0]) {
        send_command_result(command_id, false, NULL, "Missing ip", false);
        return;
    }

    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d%s", ip, port, path);

    char *resp_buf = (char *)platform_malloc_large(8192);
    if (!resp_buf) {
        send_command_result(command_id, false, NULL, "Out of memory", false);
        return;
    }

    int status = platform_http_request(url, method, body, resp_buf, 8192, timeout_ms);
    if (status < 0) {
        free(resp_buf);
        char err[128];
        snprintf(err, sizeof(err), "HTTP %.10s %.100s failed", method, url);
        send_command_result(command_id, false, NULL, err, false);
        return;
    }

    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "status", status);
    cJSON_AddStringToObject(result, "body",   resp_buf);
    free(resp_buf);

    GW_LOGD(TAG, "HTTP %s %s -> %d", method, url, status);
    send_command_result(command_id, true, result, NULL, false);
}

/* TCP_REQUEST — one send+recv, result: { body }. */
static void handle_tcp_request(const char *command_id, cJSON *payload)
{
    cJSON *ip_item      = cJSON_GetObjectItem(payload, "ip");
    cJSON *port_item    = cJSON_GetObjectItem(payload, "port");
    cJSON *data_item    = cJSON_GetObjectItem(payload, "data");
    cJSON *timeout_item = cJSON_GetObjectItem(payload, "timeoutMs");

    const char *ip        = cJSON_IsString(ip_item)     ? ip_item->valuestring    : "";
    int         port      = cJSON_IsNumber(port_item)   ? (int)port_item->valuedouble : 4028;
    const char *data      = cJSON_IsString(data_item)   ? data_item->valuestring  : "";
    int         timeout_ms = cJSON_IsNumber(timeout_item) ? (int)timeout_item->valuedouble : 8000;

    if (!ip[0]) {
        send_command_result(command_id, false, NULL, "Missing ip", false);
        return;
    }

    char *recv_buf = (char *)platform_malloc_large(4096);
    if (!recv_buf) {
        send_command_result(command_id, false, NULL, "Out of memory", false);
        return;
    }

    bool ok = platform_tcp_send_recv(ip, port, data, recv_buf, 4096, timeout_ms);
    if (!ok) {
        free(recv_buf);
        char err[64];
        snprintf(err, sizeof(err), "TCP %s:%d failed", ip, port);
        send_command_result(command_id, false, NULL, err, false);
        return;
    }

    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "body", recv_buf);
    free(recv_buf);

    GW_LOGD(TAG, "TCP %s:%d -> ok", ip, port);
    send_command_result(command_id, true, result, NULL, false);
}

/* MAC_LOOKUP — find IP for a MAC via ARP; optionally sweep CIDRs first. */
static void handle_mac_lookup(const char *command_id, cJSON *payload)
{
    cJSON *mac_item   = cJSON_GetObjectItem(payload, "mac");
    cJSON *cidrs_item = cJSON_GetObjectItem(payload, "cidrs");
    const char *mac   = cJSON_IsString(mac_item) ? mac_item->valuestring : NULL;

    if (!mac || !mac[0]) {
        send_command_result(command_id, false, NULL, "Missing mac", false);
        return;
    }

    char ip_buf[16] = "";
    bool found = platform_arp_mac_to_ip(mac, ip_buf, sizeof(ip_buf));

    /* If not in ARP cache and CIDRs provided, TCP-probe each host to warm cache. */
    if (!found && cJSON_IsArray(cidrs_item)) {
        int n = cJSON_GetArraySize(cidrs_item);
        for (int ci = 0; ci < n && !found; ci++) {
            cJSON *ci_item = cJSON_GetArrayItem(cidrs_item, ci);
            if (!cJSON_IsString(ci_item)) continue;

            const char *cidr = ci_item->valuestring;
            char ip_part[16] = "";
            int prefix = 24;
            const char *slash = strchr(cidr, '/');
            if (slash) {
                int hlen = (int)(slash - cidr);
                if (hlen > 0 && hlen < (int)sizeof(ip_part)) {
                    memcpy(ip_part, cidr, hlen);
                    ip_part[hlen] = '\0';
                }
                prefix = atoi(slash + 1);
            } else {
                strncpy(ip_part, cidr, sizeof(ip_part) - 1);
            }

            uint32_t base_ip = 0;
            parse_ipv4(ip_part, &base_ip);
            if (prefix < 16 || prefix > 30) prefix = 24;
            uint32_t host_bits = (uint32_t)(32 - prefix);
            uint32_t num_hosts = (1u << host_bits) - 2u;
            uint32_t net_addr  = base_ip & ~((1u << host_bits) - 1u);

            char host_str[16];
            GW_LOGI(TAG, "MAC_LOOKUP: ARP sweep %s (%" PRIu32 " hosts)", cidr, num_hosts);
            for (uint32_t i = 1; i <= num_hosts; i++) {
                ip_to_str(net_addr + i, host_str, sizeof(host_str));
                platform_tcp_probe(host_str, 80, 50);  /* warms ARP cache */
            }
            found = platform_arp_mac_to_ip(mac, ip_buf, sizeof(ip_buf));
        }
    }

    cJSON *result = cJSON_CreateObject();
    if (found)
        cJSON_AddStringToObject(result, "ip", ip_buf);
    else
        cJSON_AddNullToObject(result, "ip");

    GW_LOGI(TAG, "MAC_LOOKUP %s -> %s", mac, found ? ip_buf : "not found");
    send_command_result(command_id, true, result, NULL, false);
}

/* IP_LOOKUP — find MAC for an IP via ARP table. */
static void handle_ip_lookup(const char *command_id, cJSON *payload)
{
    cJSON *ip_item  = cJSON_GetObjectItem(payload, "ip");
    const char *ip  = cJSON_IsString(ip_item) ? ip_item->valuestring : NULL;

    if (!ip || !ip[0]) {
        send_command_result(command_id, false, NULL, "Missing ip", false);
        return;
    }

    char mac_buf[18] = "";
    bool found = platform_arp_ip_to_mac(ip, mac_buf, sizeof(mac_buf));

    cJSON *result = cJSON_CreateObject();
    if (found)
        cJSON_AddStringToObject(result, "mac", mac_buf);
    else
        cJSON_AddNullToObject(result, "mac");

    GW_LOGI(TAG, "IP_LOOKUP %s -> %s", ip, found ? mac_buf : "not found");
    send_command_result(command_id, true, result, NULL, false);
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
            cJSON      *id_item   = cJSON_GetObjectItem(data, "commandId");
            cJSON      *type_item = cJSON_GetObjectItem(data, "type");
            const char *command_id = cJSON_IsString(id_item)   ? id_item->valuestring   : NULL;
            const char *cmd_type   = cJSON_IsString(type_item) ? type_item->valuestring : "";

            GW_LOGI(TAG, "Command: %s (id=%s)", cmd_type, command_id ? command_id : "?");

            if (!command_id) {
                GW_LOGW(TAG, "Command missing commandId, ignoring");
                break;
            }

            if (strcmp(cmd_type, "SCAN_IP_RANGE") == 0) {
                handle_scan_ip_range(command_id, data);
            } else if (strcmp(cmd_type, "HTTP_REQUEST") == 0) {
                handle_http_request(command_id, data);
            } else if (strcmp(cmd_type, "TCP_REQUEST") == 0) {
                handle_tcp_request(command_id, data);
            } else if (strcmp(cmd_type, "MAC_LOOKUP") == 0) {
                handle_mac_lookup(command_id, data);
            } else if (strcmp(cmd_type, "IP_LOOKUP") == 0) {
                handle_ip_lookup(command_id, data);
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
     * reconnect automatically. */
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
