#pragma once

/**
 * wslink.h — C translation of gateway/wsLink/
 *
 * Mirrors the TypeScript structure:
 *   encoder.ts        → cJSON encode/decode (embedded in wslink.c)
 *   options.ts        → wslink_config_t
 *   utils.ts          → wslink_prepare_url(), PING/PONG handling
 *   requestManager.ts → internal outgoing/pending request table
 *   wsConnection.ts   → connection setup, connectionParams, keep-alive
 *   wsClient.ts       → subscribe, mutation, message routing
 *   wsLink.ts         → this header (public entry point)
 *
 * The application (gateway_core.c) calls only wslink_subscribe /
 * wslink_mutation without knowing about message IDs, JSON framing,
 * PING/PONG, or the connectionParams handshake.
 */

#ifdef ESP_PLATFORM
#  include "cJSON.h"
#else
#  include <cjson/cJSON.h>
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Result types (mirrors TRPCResponseMessage result.type) ─────────────── */

typedef enum {
    WSLINK_RESULT_STARTED,   /* subscription confirmed by server              */
    WSLINK_RESULT_DATA,      /* subscription data event or mutation response  */
    WSLINK_RESULT_STOPPED,   /* subscription stopped by server                */
    WSLINK_RESULT_ERROR,     /* error object received                         */
} wslink_result_type_t;

/* ── Callbacks ───────────────────────────────────────────────────────────── */

/**
 * Subscription event callback (mirrors the observer in wsClient.ts request()).
 *
 * type  : event kind
 * data  : result.data cJSON object for DATA events (borrowed — do not free)
 * error : error message string for ERROR events   (borrowed — do not free)
 */
typedef void (*wslink_sub_cb_t)(wslink_result_type_t type,
                                cJSON               *data,
                                const char          *error,
                                void                *user_data);

/**
 * Mutation response callback — pass NULL for fire-and-forget.
 *
 * result : result.data cJSON object on success (borrowed — do not free)
 * error  : error message string on failure     (borrowed — do not free)
 */
typedef void (*wslink_mut_cb_t)(bool        success,
                                cJSON      *result,
                                const char *error,
                                void       *user_data);

/* ── Configuration (mirrors WebSocketClientOptions in options.ts) ────────── */

typedef struct {
    /**
     * Build the connectionParams.data object sent immediately after connect.
     * (mirrors the connectionParams option in WebSocketClientOptions)
     * Returns a newly-created cJSON object; wslink takes ownership and frees it.
     * Return NULL to skip sending connectionParams.
     */
    cJSON *(*get_connection_params)(void *user_data);

    /** Called when connection is open and connectionParams have been sent.
     *  (mirrors onOpen in WebSocketClientOptions) */
    void (*on_open)(void *user_data);

    /** Called when connection is closed.
     *  (mirrors onClose in WebSocketClientOptions) */
    void (*on_close)(void *user_data);

    void *user_data;
} wslink_config_t;

/* ── Opaque client handle ────────────────────────────────────────────────── */

typedef struct wslink_client wslink_client_t;

/* ── Client lifecycle ────────────────────────────────────────────────────── */

/** Create a new wsLink client. Does not connect. */
wslink_client_t *wslink_create(const wslink_config_t *config);

void wslink_destroy(wslink_client_t *client);

/* ── URL preparation (mirrors prepareUrl() in utils.ts) ─────────────────── */

/**
 * Write the connect URL into out_buf.
 * Appends "?connectionParams=1" when the config has get_connection_params set,
 * matching the TypeScript prepareUrl() behaviour.
 */
void wslink_prepare_url(const wslink_client_t *client,
                        const char            *base_url,
                        char                  *out_buf,
                        size_t                 out_size);

/* ── Operations ──────────────────────────────────────────────────────────── */

/**
 * Send a tRPC subscription.
 * (mirrors WsClient.request() with op.type == "subscription")
 *
 * Returns the subscription ID (≥ 1), or -1 on error.
 * The callback fires with STARTED → DATA* → (STOPPED | ERROR).
 * Subscriptions survive reconnects: wslink replays them on each connect.
 */
int wslink_subscribe(wslink_client_t *client,
                     const char      *path,
                     wslink_sub_cb_t  cb,
                     void            *user_data);

/**
 * Send a subscription.stop message.
 * (mirrors the cleanup function returned by WsClient.request())
 */
void wslink_stop_subscription(wslink_client_t *client, int sub_id);

/**
 * Send a tRPC mutation.
 * (mirrors WsClient.request() with op.type == "mutation")
 *
 * input    : cJSON object for params.input — wslink takes ownership (will free).
 *            Pass NULL for mutations with no input.
 * cb       : response callback, or NULL for fire-and-forget.
 * Returns the message ID (≥ 1), or -1 on error.
 */
int wslink_mutation(wslink_client_t *client,
                    const char      *path,
                    cJSON           *input,
                    wslink_mut_cb_t  cb,
                    void            *user_data);

/* ── Platform event bridge ───────────────────────────────────────────────── */

/**
 * Call when the WebSocket transport has connected.
 * Sends connectionParams then replays all registered subscriptions.
 * (mirrors WsConnection.open() + WsClient.setupWebSocketListeners 'open' handler)
 */
void wslink_on_connected(wslink_client_t *client);

/**
 * Call when the WebSocket transport has disconnected.
 * (mirrors WsClient.setupWebSocketListeners 'close' handler)
 */
void wslink_on_disconnected(wslink_client_t *client);

/**
 * Call for each incoming text frame from the server.
 * Handles PING/PONG at transport level, then routes tRPC response messages.
 * (mirrors WsClient.setupWebSocketListeners 'message' handler)
 */
void wslink_on_message(wslink_client_t *client, const char *data, int len);

/* ── Status ──────────────────────────────────────────────────────────────── */

/** True when the transport is connected and connectionParams have been sent. */
bool wslink_is_open(const wslink_client_t *client);

#ifdef __cplusplus
}
#endif
