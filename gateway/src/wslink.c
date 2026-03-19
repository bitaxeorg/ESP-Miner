/**
 * wslink.c — C translation of gateway/wsLink/
 *
 * Section headings below map to the TypeScript source files:
 *
 *   [encoder.ts]        JSON encode/decode via cJSON
 *   [utils.ts]          URL preparation, PING/PONG constants
 *   [options.ts]        Default values for config fields
 *   [requestManager.ts] Outgoing / pending request table
 *   [wsConnection.ts]   Connection setup, connectionParams, keep-alive
 *   [wsClient.ts]       subscribe(), mutation(), on_message() routing
 */

#ifdef ESP_PLATFORM
#  include "cJSON.h"
#else
#  include <cjson/cJSON.h>
#endif

#include "wslink.h"
#include "gateway_platform.h"   /* platform_ws_send, GW_LOG* */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "wslink";

/* ═══════════════════════════════════════════════════════════════════════════
 * [requestManager.ts]  Request table
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Mirrors the RequestManager class:
 *   outgoing  → registered but not yet sent (no active WS connection)
 *   pending   → sent, awaiting response(s)
 *   Subscriptions stay pending until stopped or errored.
 */

#define WSLINK_MAX_REQUESTS 16

typedef enum { REQ_SUBSCRIPTION, REQ_MUTATION } req_type_t;
typedef enum { REQ_OUTGOING, REQ_PENDING }      req_state_t;

typedef struct {
    bool            active;
    int             id;
    req_type_t      type;
    req_state_t     state;
    char            path[64];
    wslink_sub_cb_t sub_cb;
    wslink_mut_cb_t mut_cb;
    void           *user_data;
} wslink_req_t;

/* ═══════════════════════════════════════════════════════════════════════════
 * [wsClient.ts]  Client struct
 * ═══════════════════════════════════════════════════════════════════════════ */

struct wslink_client {
    wslink_config_t config;
    bool            connected;   /* transport is up and connectionParams sent */
    int             next_id;     /* monotonically increasing message counter  */
    wslink_req_t    reqs[WSLINK_MAX_REQUESTS];
    int             req_count;
};

/* ── requestManager helpers ─────────────────────────────────────────────── */

/* find_request mirrors RequestManager.getPendingRequest() */
static wslink_req_t *find_request(wslink_client_t *c, int id)
{
    for (int i = 0; i < c->req_count; i++) {
        if (c->reqs[i].active && c->reqs[i].id == id)
            return &c->reqs[i];
    }
    return NULL;
}

/* alloc_request mirrors the internal slot allocation in RequestManager.register() */
static wslink_req_t *alloc_request(wslink_client_t *c)
{
    for (int i = 0; i < c->req_count; i++) {
        if (!c->reqs[i].active)
            return &c->reqs[i];
    }
    if (c->req_count < WSLINK_MAX_REQUESTS)
        return &c->reqs[c->req_count++];
    GW_LOGE(TAG, "Request table full");
    return NULL;
}

/* free_request mirrors RequestManager.delete() */
static void free_request(wslink_req_t *req)
{
    memset(req, 0, sizeof(*req));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * [encoder.ts]  JSON encode / decode
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * jsonEncoder.encode → ws_send_json()
 * jsonEncoder.decode → cJSON_ParseWithLength() (used inline in wslink_on_message)
 */

static void ws_send_json(cJSON *root)
{
    char *s = cJSON_PrintUnformatted(root);
    if (s) {
        platform_ws_send(s, (int)strlen(s));
        free(s);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * [utils.ts]  URL preparation and PING/PONG
 * ═══════════════════════════════════════════════════════════════════════════ */

/* prepareUrl(): append ?connectionParams=1 when connectionParams are used. */
void wslink_prepare_url(const wslink_client_t *client,
                        const char            *base_url,
                        char                  *out_buf,
                        size_t                 out_size)
{
    if (!client->config.get_connection_params) {
        snprintf(out_buf, out_size, "%s", base_url);
        return;
    }

    const char *sep = strchr(base_url, '?') ? "&" : "?";
    snprintf(out_buf, out_size, "%s%sconnectionParams=1", base_url, sep);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * [wsConnection.ts]  Connection setup and keep-alive
 * ═══════════════════════════════════════════════════════════════════════════ */

/* buildConnectionMessage(): { method:"connectionParams", data:{...} }
 * Sent immediately after the WebSocket opens — before any operations. */
static void send_connection_params(wslink_client_t *c)
{
    if (!c->config.get_connection_params) {
        GW_LOGI(TAG, "send_connection_params: no get_connection_params callback");
        return;
    }

    cJSON *data = c->config.get_connection_params(c->config.user_data);
    if (!data) {
        GW_LOGE(TAG, "send_connection_params: get_connection_params returned NULL");
        return;
    }

    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "method", "connectionParams");
    cJSON_AddItemToObject(msg, "data", data);   /* data ownership transferred */

    char *s = cJSON_PrintUnformatted(msg);
    if (s) {
        GW_LOGI(TAG, "TX connectionParams: %s", s);
        platform_ws_send(s, (int)strlen(s));
        free(s);
    }
    cJSON_Delete(msg);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * [wsClient.ts]  Wire-format helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Send { id, method:"subscription", params:{ path } }
 * Mirrors the serialisation in WsClient.request() for subscriptions. */
static void send_subscription_msg(wslink_client_t *c, wslink_req_t *req)
{
    cJSON *msg    = cJSON_CreateObject();
    cJSON *params = cJSON_CreateObject();
    cJSON_AddNumberToObject(msg, "id", req->id);
    cJSON_AddStringToObject(msg, "method", "subscription");
    cJSON_AddStringToObject(params, "path", req->path);
    cJSON_AddItemToObject(msg, "params", params);
    ws_send_json(msg);
    cJSON_Delete(msg);
}

/* Send { id, method:"mutation", params:{ path, input? } }
 * input ownership is transferred and freed with the message. */
static void send_mutation_msg(int id, const char *path, cJSON *input)
{
    cJSON *msg    = cJSON_CreateObject();
    cJSON *params = cJSON_CreateObject();
    cJSON_AddNumberToObject(msg, "id", id);
    cJSON_AddStringToObject(msg, "method", "mutation");
    cJSON_AddStringToObject(params, "path", path);
    if (input)
        cJSON_AddItemToObject(params, "input", input);  /* ownership transferred */
    cJSON_AddItemToObject(msg, "params", params);
    ws_send_json(msg);
    cJSON_Delete(msg);
}

/* Send { id, method:"subscription.stop" }
 * Mirrors the cleanup function returned by WsClient.request(). */
static void send_subscription_stop(int id)
{
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddNumberToObject(msg, "id", id);
    cJSON_AddStringToObject(msg, "method", "subscription.stop");
    ws_send_json(msg);
    cJSON_Delete(msg);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * [wsClient.ts]  handleResponseMessage() + handleIncomingRequest()
 * ═══════════════════════════════════════════════════════════════════════════ */

static void handle_response(wslink_client_t *c,
                             int              msg_id,
                             cJSON           *result_obj,   /* may be NULL */
                             cJSON           *error_obj)    /* may be NULL */
{
    wslink_req_t *req = find_request(c, msg_id);
    if (!req) return;   /* unsolicited or already completed */

    /* ── error path ──────────────────────────────────────────────────────── */
    if (error_obj) {
        cJSON      *msg_item = cJSON_GetObjectItem(error_obj, "message");
        const char *err      = cJSON_IsString(msg_item) ? msg_item->valuestring
                                                        : "unknown error";
        if (req->type == REQ_SUBSCRIPTION && req->sub_cb)
            req->sub_cb(WSLINK_RESULT_ERROR, NULL, err, req->user_data);
        else if (req->type == REQ_MUTATION && req->mut_cb)
            req->mut_cb(false, NULL, err, req->user_data);
        free_request(req);
        return;
    }

    /* ── result path ─────────────────────────────────────────────────────── */
    cJSON      *type_item = cJSON_GetObjectItem(result_obj, "type");
    const char *type_str  = cJSON_IsString(type_item) ? type_item->valuestring : "";
    cJSON      *data      = cJSON_GetObjectItem(result_obj, "data");

    if (strcmp(type_str, "started") == 0) {
        /* Subscription confirmed — transition outgoing → pending */
        req->state = REQ_PENDING;
        if (req->sub_cb)
            req->sub_cb(WSLINK_RESULT_STARTED, NULL, NULL, req->user_data);

    } else if (strcmp(type_str, "data") == 0) {
        if (req->type == REQ_SUBSCRIPTION) {
            /* Subscription data: keep the request open */
            if (req->sub_cb)
                req->sub_cb(WSLINK_RESULT_DATA, data, NULL, req->user_data);
        } else {
            /* Mutation response: complete and free */
            if (req->mut_cb)
                req->mut_cb(true, data, NULL, req->user_data);
            free_request(req);
        }

    } else if (strcmp(type_str, "stopped") == 0) {
        if (req->sub_cb)
            req->sub_cb(WSLINK_RESULT_STOPPED, NULL, NULL, req->user_data);
        free_request(req);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

wslink_client_t *wslink_create(const wslink_config_t *config)
{
    wslink_client_t *c = calloc(1, sizeof(wslink_client_t));
    if (!c) return NULL;
    c->config    = *config;
    c->next_id   = 1;
    c->connected = false;
    return c;
}

void wslink_destroy(wslink_client_t *client)
{
    free(client);
}

bool wslink_is_open(const wslink_client_t *client)
{
    return client && client->connected;
}

/* ── wsLink.ts / WsClient.request() — subscription ─────────────────────── */

int wslink_subscribe(wslink_client_t *c,
                     const char      *path,
                     wslink_sub_cb_t  cb,
                     void            *user_data)
{
    wslink_req_t *req = alloc_request(c);
    if (!req) return -1;

    req->active    = true;
    req->id        = c->next_id++;
    req->type      = REQ_SUBSCRIPTION;
    req->state     = REQ_OUTGOING;
    req->sub_cb    = cb;
    req->user_data = user_data;
    strncpy(req->path, path, sizeof(req->path) - 1);

    if (c->connected) {
        req->state = REQ_PENDING;
        send_subscription_msg(c, req);
    }
    /* If not connected, stays outgoing until wslink_on_connected() replays it. */

    return req->id;
}

/* Mirrors the cleanup() returned by WsClient.request() for subscriptions. */
void wslink_stop_subscription(wslink_client_t *c, int sub_id)
{
    wslink_req_t *req = find_request(c, sub_id);
    if (!req || req->type != REQ_SUBSCRIPTION) return;

    if (c->connected)
        send_subscription_stop(sub_id);

    free_request(req);
}

/* ── wsLink.ts / WsClient.request() — mutation ──────────────────────────── */

int wslink_mutation(wslink_client_t *c,
                    const char      *path,
                    cJSON           *input,
                    wslink_mut_cb_t  cb,
                    void            *user_data)
{
    int id = c->next_id++;

    if (!c->connected) {
        GW_LOGW(TAG, "mutation(%s) dropped — not connected", path);
        if (input) cJSON_Delete(input);
        if (cb) cb(false, NULL, "Not connected", user_data);
        return -1;
    }

    /* Register in the request table only when a callback is expected. */
    if (cb) {
        wslink_req_t *req = alloc_request(c);
        if (req) {
            req->active    = true;
            req->id        = id;
            req->type      = REQ_MUTATION;
            req->state     = REQ_PENDING;
            req->mut_cb    = cb;
            req->user_data = user_data;
            strncpy(req->path, path, sizeof(req->path) - 1);
        }
    }

    /* input ownership transferred to send_mutation_msg → cJSON_Delete(msg) */
    send_mutation_msg(id, path, input);
    return id;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Platform event bridge
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * wslink_on_connected
 * Mirrors WsConnection.open() + WsClient.setupWebSocketListeners 'open':
 *   1. Send connectionParams (wsConnection.ts: buildConnectionMessage)
 *   2. Flush / replay outgoing subscriptions (wsClient.ts: reconnect path)
 */
void wslink_on_connected(wslink_client_t *c)
{
    c->connected = true;

    send_connection_params(c);

    /* Replay all registered subscriptions — matches the TypeScript reconnect
     * path where pending requests are re-sent after a new connection opens. */
    for (int i = 0; i < c->req_count; i++) {
        wslink_req_t *req = &c->reqs[i];
        if (!req->active || req->type != REQ_SUBSCRIPTION) continue;
        req->state = REQ_PENDING;
        send_subscription_msg(c, req);
    }

    if (c->config.on_open)
        c->config.on_open(c->config.user_data);
}

/**
 * wslink_on_disconnected
 * Mirrors WsClient.setupWebSocketListeners 'close':
 *   - Subscriptions remain registered (will be replayed on reconnect).
 *   - In-flight mutations are lost (no persistent queue in embedded context).
 */
void wslink_on_disconnected(wslink_client_t *c)
{
    c->connected = false;

    /* Drop pending mutations — they cannot be retried without the caller
     * knowing.  Subscriptions stay in the table for replay on reconnect. */
    for (int i = 0; i < c->req_count; i++) {
        wslink_req_t *req = &c->reqs[i];
        if (!req->active || req->type != REQ_MUTATION) continue;
        if (req->mut_cb)
            req->mut_cb(false, NULL, "WebSocket closed", req->user_data);
        free_request(req);
    }

    if (c->config.on_close)
        c->config.on_close(c->config.user_data);
}

/**
 * wslink_on_message
 * Mirrors WsClient.setupWebSocketListeners 'message':
 *   1. Handle PING/PONG text frames (wsConnection.ts ping listener +
 *      wsClient.ts ['PING','PONG'] guard).
 *   2. Decode JSON (encoder.ts: jsonEncoder.decode).
 *   3. Route to handle_response() (wsClient.ts: handleResponseMessage /
 *      handleIncomingRequest).
 */
void wslink_on_message(wslink_client_t *c, const char *data, int len)
{
    /* [wsConnection.ts] Respond to server keep-alive PING with PONG.
     * [wsClient.ts]     Ignore PONG — don't attempt JSON parse on either. */
    if (len == 4 && memcmp(data, "PING", 4) == 0) {
        platform_ws_send("PONG", 4);
        return;
    }
    if (len == 4 && memcmp(data, "PONG", 4) == 0) {
        return;
    }

    /* [encoder.ts] jsonEncoder.decode */
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        GW_LOGW(TAG, "Failed to parse WS message");
        return;
    }

    cJSON *id_item    = cJSON_GetObjectItem(root, "id");
    cJSON *result_obj = cJSON_GetObjectItem(root, "result");
    cJSON *error_obj  = cJSON_GetObjectItem(root, "error");

    /* [wsClient.ts] handleIncomingRequest: server-initiated messages
     * (e.g. method:"reconnect") — reconnect is managed by gateway_core_run. */
    if (!cJSON_IsNumber(id_item)) {
        cJSON_Delete(root);
        return;
    }

    /* [wsClient.ts] handleResponseMessage */
    int msg_id = id_item->valueint;

    if (error_obj)
        handle_response(c, msg_id, NULL, error_obj);
    else if (cJSON_IsObject(result_obj))
        handle_response(c, msg_id, result_obj, NULL);

    cJSON_Delete(root);
}
