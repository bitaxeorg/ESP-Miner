/**
 * gateway_main.c — macOS entry point for the standalone gateway process.
 *
 * Reads credentials from environment variables (or .env file pattern):
 *   HASHLY_CID     — client ID
 *   HASHLY_SEC     — client secret
 *   HASHLY_URL     — WebSocket URL (wss://...)
 *   HASHLY_VER     — optional version string (default: "mac-1.0")
 *
 * Usage:
 *   export HASHLY_CID=my-client-id
 *   export HASHLY_SEC=my-secret
 *   export HASHLY_URL=wss://gateway.hashly.io/ws
 *   ./gateway
 *
 * Build:
 *   cmake -B build gateway/platform/macos && cmake --build build
 *   # or use the provided CMakeLists.txt via 'cmake -S gateway/platform/macos -B build'
 */

#include "gateway_core.h"
#include "gateway_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

static GatewayModule g_gw_module;

int main(void)
{
    // ── Read credentials from environment ───────────────────────────────────
    const char *client_id     = getenv("HASHLY_CID");
    const char *client_secret = getenv("HASHLY_SEC");
    const char *ws_url        = getenv("HASHLY_URL");
    const char *version       = getenv("HASHLY_VER");

    if (!client_id || !*client_id) {
        fprintf(stderr, "Error: HASHLY_CID environment variable not set\n");
        return 1;
    }
    if (!client_secret || !*client_secret) {
        fprintf(stderr, "Error: HASHLY_SEC environment variable not set\n");
        return 1;
    }
    if (!ws_url || !*ws_url) {
        fprintf(stderr, "Error: HASHLY_URL environment variable not set\n");
        return 1;
    }
    if (!version || !*version) {
        version = "mac-1.0";
    }

    // ── One-time global init for libcurl ────────────────────────────────────
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // ── Start gateway ───────────────────────────────────────────────────────
    memset(&g_gw_module, 0, sizeof(g_gw_module));

    GatewayConfig cfg = {
        .client_id     = client_id,
        .client_secret = client_secret,
        .url           = ws_url,
        .version       = version,
    };

    gateway_core_init(&cfg, &g_gw_module);

    printf("[gateway] Starting (url=%s, clientId=%s, version=%s)\n",
           ws_url, client_id, version);

    // Blocks indefinitely; reconnects automatically on disconnect
    gateway_core_run();

    curl_global_cleanup();
    return 0;
}
