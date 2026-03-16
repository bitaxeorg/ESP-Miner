#ifndef MINER_ADAPTER_H_
#define MINER_ADAPTER_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MINER_TYPE_UNKNOWN = 0,
    MINER_TYPE_BITAXE,
    MINER_TYPE_HAMMER,
    MINER_TYPE_NERD,
    MINER_TYPE_CANAAN,
} MinerType;

// Discovered device from network scan
typedef struct {
    char ip[16];
    char mac[18];
    MinerType type;
    char model[32];
    char hostname[33];
    float hashrate;
    bool reachable;
} DiscoveredMiner;

#define SCAN_MAX_RESULTS 32

typedef struct {
    bool scanning;
    bool complete;
    volatile bool stop_requested;
    int result_count;
    DiscoveredMiner results[SCAN_MAX_RESULTS];
    // Diagnostic error counts from last scan
    int err_alloc;
    int err_init;
    int err_perform;
    int err_status;
    int err_parse;
} ScanState;

// Forward declaration
struct PeerMinerState_;

// Callback invoked for each miner found during scan (for streaming results)
typedef void (*scan_found_cb_t)(const DiscoveredMiner *miner, void *ctx);

// Functions
const char *miner_type_to_string(MinerType type);
MinerType miner_type_from_string(const char *str);
bool poll_miner(const char *ip, MinerType type, struct PeerMinerState_ *peer);
bool scan_ip(const char *ip, DiscoveredMiner *result);
void run_network_scan(ScanState *state, scan_found_cb_t on_found, void *cb_ctx);

// Extract a value from MM ID0 bracketed format: "Key[value]"
bool mmget(const char *mmid0, const char *key, char *out, int out_size);

#endif /* MINER_ADAPTER_H_ */
