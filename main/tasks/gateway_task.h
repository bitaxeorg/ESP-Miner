#ifndef GATEWAY_TASK_H_
#define GATEWAY_TASK_H_

#include <stdbool.h>
#include <stdint.h>
#include "miner_adapter.h"

#define GATEWAY_MAX_PEERS 30
#define GATEWAY_IP_MAX_LEN 16
#define MAX_POOLS 3

typedef enum {
    PEER_STATUS_UNKNOWN,
    PEER_STATUS_ONLINE,
    PEER_STATUS_OFFLINE,
    PEER_STATUS_ERROR
} PeerStatus;

typedef struct {
    char url[96];
    char user[96];
    bool active;
    double stratum_diff;
    uint64_t accepted;
    uint64_t rejected;
} PoolInfo;

typedef struct PeerMinerState_ {
    char ip[GATEWAY_IP_MAX_LEN];
    MinerType type;
    PeerStatus status;
    int consecutive_failures;

    // Cached stats from last successful poll
    float hashrate;
    float temp;
    float power;
    float voltage;
    float frequency;
    uint64_t shares_accepted;
    uint64_t shares_rejected;
    uint32_t uptime_seconds;
    char mac_addr[18];
    char hostname[33];
    char asic_model[32];      // ASIC chip (e.g. "BM1370", "A3197S")
    char device_model[64];    // Product name (e.g. "NerdQAxe++", "Avalon Nano3s")
    float vr_temp;            // Voltage regulator temp
    float current;            // Current in mA
    int fan_pct;
    int fan_rpm;              // Average fan RPM (AxeOS: avg of fanrpm/fan2rpm, Canaan: avg of Fan1-Fan4)
    int wifi_rssi;
    int found_blocks;
    double best_diff;         // Best share difficulty (lifetime)
    double best_session_diff; // Best share difficulty (this session, AxeOS only)
    double pool_difficulty;   // Active pool's stratum difficulty
    int64_t last_seen;        // timestamp of last successful poll (microseconds)

    // Aggregated temperatures
    float temp_avg;           // Average chip temp (Bitaxe: computed from temp/temp2, Canaan: TAvg)
    float temp_max;           // Max chip temp (Bitaxe: max(temp,temp2), Canaan: TMax)

    // Pool info
    PoolInfo pools[MAX_POOLS];
    int pool_count;
} PeerMinerState;

typedef struct {
    int peer_count;
    PeerMinerState peers[GATEWAY_MAX_PEERS];
    bool is_polling;
    ScanState SCAN_STATE;
} GatewayModule;

// No gateway_task — ws_client_task handles everything now.
// gateway_init() is called from main.c with a void* cast.
void gateway_init(void *global_state);

#endif /* GATEWAY_TASK_H_ */
