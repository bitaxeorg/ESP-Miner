#pragma once

#include <stdint.h>
#include <stdbool.h>

// ============================================================
// Miner type identifiers
// ============================================================

typedef enum {
    MINER_TYPE_UNKNOWN = 0,
    MINER_TYPE_BITAXE,   // AxeOs
    MINER_TYPE_HAMMER,   // Hammer
    MINER_TYPE_NERD,     // Nerd
    MINER_TYPE_CANAAN,   // AvalonQ / AvalonNano3s / AvalonMini3
} MinerType;

typedef enum {
    PEER_STATUS_UNKNOWN = 0,
    PEER_STATUS_ONLINE,
    PEER_STATUS_OFFLINE,
    PEER_STATUS_ERROR,
} PeerStatus;

// ============================================================
// Pool info per miner
// ============================================================

#define POOL_URL_MAX_LEN  256
#define POOL_USER_MAX_LEN 128

typedef struct {
    char url[POOL_URL_MAX_LEN];
    char user[POOL_USER_MAX_LEN];
    bool active;
    double stratum_diff;
    uint64_t accepted;
    uint64_t rejected;
} PoolInfo;

// ============================================================
// Per-peer miner state (cached result of a poll)
// ============================================================

#define GATEWAY_IP_MAX_LEN    16
#define GATEWAY_MAX_PEERS     30

typedef struct PeerMinerState_ {
    char ip[GATEWAY_IP_MAX_LEN];
    MinerType type;
    PeerStatus status;

    // Core mining stats
    float   hashrate;
    float   temp;
    float   power;
    float   voltage;
    float   frequency;
    uint64_t shares_accepted;
    uint64_t shares_rejected;
    uint32_t uptime_seconds;

    // Extended stats
    float    vr_temp;
    float    current;
    int      fan_pct;
    int      fan_rpm;
    int      wifi_rssi;
    int      found_blocks;
    double   best_diff;
    double   best_session_diff;
    double   pool_difficulty;
    float    temp_avg;
    float    temp_max;

    // Device identity
    char mac_addr[18];
    char hostname[33];
    char asic_model[32];
    char device_model[64];

    // Pool list
    PoolInfo pools[3];
    int      pool_count;

    // Internal tracking
    int      consecutive_failures;
    int64_t  last_seen;   // platform_time_ms() at last successful poll
} PeerMinerState;

// ============================================================
// Scan state
// ============================================================

#define SCAN_MAX_RESULTS 32

typedef struct {
    char ip[GATEWAY_IP_MAX_LEN];
    char mac[18];
    MinerType type;
    char model[32];
    char hostname[33];
    float hashrate;
    bool reachable;
} DiscoveredMiner;

typedef struct {
    bool scanning;
    bool complete;
    volatile bool stop_requested;
    int result_count;
    DiscoveredMiner results[SCAN_MAX_RESULTS];
    // Diagnostic counters from last scan
    int err_alloc;
    int err_init;
    int err_perform;
    int err_status;
    int err_parse;
} ScanState;

// ============================================================
// Gateway module — live peer state + scan state
// ============================================================

typedef struct {
    int peer_count;
    PeerMinerState peers[GATEWAY_MAX_PEERS];
    bool is_polling;
    ScanState SCAN_STATE;
} GatewayModule;
