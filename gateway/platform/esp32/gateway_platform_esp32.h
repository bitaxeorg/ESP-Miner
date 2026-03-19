#pragma once

#include "gateway_types.h"

/**
 * Push a snapshot of this device's own miner stats into the gateway platform.
 *
 * Call this from ws_client_task.c (or wherever GlobalState is accessible)
 * whenever the local hashrate / power / temperature values are refreshed.
 * gateway_core will call platform_get_self_stats() which serves from this cache.
 *
 * stats: pointer to a fully populated PeerMinerState.  Copied on entry.
 */
void platform_esp32_update_self_stats(const PeerMinerState *stats);
