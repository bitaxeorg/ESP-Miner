#pragma once

#include <stdbool.h>
#include "gateway_types.h"

// ============================================================
// Gateway core — tRPC WebSocket client
//
// Usage:
//   1. gateway_core_init(&cfg, &gw_module)
//   2. gateway_core_run()        ← blocks indefinitely (reconnects on drop)
//
// On ESP32: called from a FreeRTOS task (ws_client_task).
// On macOS: called from main().
// ============================================================

typedef struct {
    const char *client_id;
    const char *client_secret;
    const char *url;         // wss:// or ws://
    const char *version;     // firmware / software version string
} GatewayConfig;

/**
 * Initialise the core.  Must be called before gateway_core_run().
 *
 * gw_module: pointer to the GatewayModule to use for peer state.
 *   On ESP32 pass &global_state->GATEWAY_MODULE so gateway_task.c
 *   can still read live state for the local HTTP API.
 *   On macOS pass a static/heap-allocated GatewayModule.
 */
void gateway_core_init(const GatewayConfig *cfg, GatewayModule *gw_module);

/**
 * Run the gateway event loop.  Never returns under normal operation.
 * Reconnects automatically on disconnect.
 */
void gateway_core_run(void);

// Status accessors — safe to call from any thread/task
bool        gateway_core_is_connected(void);
bool        gateway_core_is_authenticated(void);
