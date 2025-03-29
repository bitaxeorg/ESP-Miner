#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>

typedef struct
{
    float temp;
    float temp2;
    float hashing_speed;
    int invalid_shares; // not implemented
    int valid_shares;   // not implemented
    int difficulty;
    float best_difficulty;
    int pool_errors;
    int accepted;
    int not_accepted;
    int total_uptime;
    float total_best_difficulty;
    int uptime;
    int blocks_found;
    int total_blocks_found;
    int duplicate_hashes; // not implemented
    float pwr_vin;
    float pwr_iin;
    float pwr_pin;
    float pwr_vout;
    float pwr_iout;
    float pwr_pout;
} Stats;

typedef struct
{
    char *host;
    int port;
    char *token;
    char *org;
    char *bucket;
    char *prefix;
    Stats stats;
    pthread_mutex_t lock;
} Influx;

Influx *influx_init(const char *host, int port, const char *token, const char *bucket, const char *org, const char *prefix);
void influx_write(Influx *influx);
bool load_last_values(Influx *influx);
bool bucket_exists(Influx *influx);
bool influx_ping(Influx *influx);

#ifdef __cplusplus
}
#endif
