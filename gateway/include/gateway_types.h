#pragma once

#include <stdbool.h>

// ── Scan state ────────────────────────────────────────────────────────────────

/**
 * Lightweight scan state — just enough for SCAN_IP_RANGE command handling.
 * The heavy PeerMinerState / DiscoveredMiner structures are gone:
 * all adapter logic and miner state now lives on the server.
 */
typedef struct {
    bool          scanning;
    volatile bool stop_requested;
} GatewayScanState;

// ── Gateway module ────────────────────────────────────────────────────────────

typedef struct {
    GatewayScanState scan_state;
} GatewayModule;
