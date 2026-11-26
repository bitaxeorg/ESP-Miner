#ifndef TEST_STUB_GLOBAL_STATE_H
#define TEST_STUB_GLOBAL_STATE_H

#include <stdbool.h>
#include <stdint.h>
#include "device_config.h"

#define MAX_POOLS 8

typedef struct PoolConfig {
    char *url;
    uint16_t port;
    stratum_protocol_t protocol;
} PoolConfig;

typedef struct PoolSession {
    uint64_t work_received;
    char pool_connection_info[64];
} PoolSession;

typedef struct SystemModule {
    PoolConfig pools[MAX_POOLS];
    PoolSession pool_sessions[MAX_POOLS];
    uint16_t primary_pool_index;
    uint16_t secondary_pool_index;
    bool use_fallback_stratum;
    bool is_using_fallback;
    float process_time;
    bool mining_paused;
    bool pools_unavailable;
    bool hardware_fault;
} SystemModule;

typedef struct GlobalState {
    SystemModule SYSTEM_MODULE;
} GlobalState;

#endif /* TEST_STUB_GLOBAL_STATE_H */
