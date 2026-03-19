/**
 * main/tasks/miner_adapter.h — compatibility shim
 *
 * The miner adapter implementation has moved to the portable gateway component
 * (gateway/src/miner_adapter.c).  This header keeps existing includes in
 * main/ working by re-exporting the types and function declarations.
 *
 * Types (MinerType, DiscoveredMiner, ScanState, PeerMinerState, etc.) now live
 * in gateway_types.h; this header forwards them by including that file.
 */
#ifndef MINER_ADAPTER_H_
#define MINER_ADAPTER_H_

#include <stdbool.h>
#include "gateway_types.h"   // MinerType, DiscoveredMiner, ScanState, PeerMinerState, ...

// Callback invoked for each miner found during a network scan (streaming)
typedef void (*scan_found_cb_t)(const DiscoveredMiner *miner, void *ctx);

// ── Function declarations ─────────────────────────────────────────────────────
// Implemented in gateway/src/miner_adapter.c (compiled as part of the
// "gateway" ESP-IDF component that main PRIV_REQUIRES).

const char *miner_type_to_string(MinerType type);
MinerType   miner_type_from_string(const char *str);
const char *miner_type_to_adapter_type(MinerType type);

bool poll_miner(const char *ip, MinerType type, PeerMinerState *peer);
bool scan_ip(const char *ip, DiscoveredMiner *result);
void run_network_scan(ScanState *state, scan_found_cb_t on_found, void *cb_ctx);

bool mmget(const char *mmid0, const char *key, char *out, int out_size);

#endif /* MINER_ADAPTER_H_ */
