#ifndef POOL_MODULE_H_
#define POOL_MODULE_H_

#include <stdint.h>
typedef struct
{
    // The URL of the mining pool.
    char * url;
    // The port number on which the mining pool operates.
    uint16_t port;
    // The username for authenticating with the mining pool.
    char * user;
    // The password for authenticating with the mining pool.
    char * pass;
    // The suggested difficulty level set on the mining pool.
    uint16_t difficulty;
     // A flag indicating whether the mining pool supports extranonce subscription.
    bool extranonce_subscribe;
} stratum_pool;

typedef struct
{
    // The configured pools to connect to.
    stratum_pool pools[2];
    
    // The currently active pool.
    uint8_t active_pool_idx;

    // The difficulty level set on the active mining pool.
    uint16_t pool_difficulty;

    // The average response time of the main mining pool to requests.
    double response_time;

    // A flag indicating if the system is currently using the fallback pool instead of the main one.
    uint8_t default_pool_idx;
}PoolModule;

extern PoolModule POOL_MODULE;
#endif
