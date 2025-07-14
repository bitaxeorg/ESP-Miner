#pragma once

typedef struct
{
    // The URL of the main mining pool.
    char * pool_url;

    // The URL of the fallback mining pool in case the main one fails.
    char * fallback_pool_url;

    // The port number on which the main mining pool operates.
    uint16_t pool_port;

    // The port number on which the fallback mining pool operates.
    uint16_t fallback_pool_port;

    // The username for authenticating with the main mining pool.
    char * pool_user;

    // The username for authenticating with the fallback mining pool.
    char * fallback_pool_user;

    // The password for authenticating with the main mining pool.
    char * pool_pass;

    // The password for authenticating with the fallback mining pool.
    char * fallback_pool_pass;

    // The difficulty level set on the main mining pool.
    uint16_t pool_difficulty;

    // The difficulty level set on the fallback mining pool.
    uint16_t fallback_pool_difficulty;

    // A flag indicating whether the main mining pool supports extranonce subscription.
    bool pool_extranonce_subscribe;

    // A flag indicating whether the fallback mining pool supports extranonce subscription.
    bool fallback_pool_extranonce_subscribe;

    // The average response time of the main mining pool to requests.
    double response_time;

    // A flag indicating if the system is currently using the fallback pool instead of the main one.
    bool is_using_fallback;
}PoolModule;

extern PoolModule POOL_MODULE;