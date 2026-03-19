#pragma once

#include <stdbool.h>
#include "gateway_types.h"

// ============================================================
// Miner adapter — discover and poll local ASIC miners
// ============================================================

/** Convert MinerType ↔ adapter string (e.g. "AxeOs", "Hammer", "Nerd", "AvalonNano3s"). */
const char *miner_type_to_string(MinerType type);
MinerType   miner_type_from_string(const char *str);

/**
 * Convert MinerType to the MinerAdapterType string used by the server
 * (matches the TypeScript MinerAdapterType enum: "AxeOs", "Hammer", "Nerd",
 * "AvalonNano3s", "AvalonQ", "AvalonMini3").
 */
const char *miner_type_to_adapter_type(MinerType type);

/** Poll a single miner at ip; fills *peer on success. Returns true if online. */
bool poll_miner(const char *ip, MinerType type, PeerMinerState *peer);

/** Probe a single IP. Returns true and fills *result if a miner was found. */
bool scan_ip(const char *ip, DiscoveredMiner *result);

// Callback invoked for each miner found during a network scan (streaming)
typedef void (*scan_found_cb_t)(const DiscoveredMiner *miner, void *ctx);

/** Run a full subnet scan, streaming results via on_found callback. */
void run_network_scan(ScanState *state, scan_found_cb_t on_found, void *cb_ctx);

/** Extract a value from an MM ID0 bracketed string: "Key[value]". */
bool mmget(const char *mmid0, const char *key, char *out, int out_size);
