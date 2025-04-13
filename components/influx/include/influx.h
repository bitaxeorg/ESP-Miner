#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float asicTemp;
    float vrTemp;
    float hashingSpeed;
    int invalidShares;
    int validShares;
    int difficulty;
    float bestSessionDifficulty;
    int poolErrors;
    int accepted;
    int notAccepted;
    int totalUptime;
    float totalBestDifficulty;
    int uptime;
    int blocksFound;
    int totalBlocksFound;
    int duplicateHashes;
    float voltage;
    float current;
    float power;
} influx_stats_t;

typedef struct {
    char *host;
    int port;
    char *token;
    char *org;
    char *bucket;
    char *prefix;
    char auth_header[128];
    char *big_buffer;
    pthread_mutex_t lock;
    influx_stats_t stats;
} influx_client_t;

/**
 * @brief Initialize the InfluxDB client
 *
 * @param client Pointer to the client structure to initialize
 * @param host InfluxDB server hostname
 * @param port InfluxDB server port
 * @param token Authentication token
 * @param bucket Bucket name
 * @param org Organization name
 * @param prefix Metric prefix
 * @return true if initialization successful
 * @return false if initialization failed
 */
bool influx_init(influx_client_t *client, const char *host, int port,
                const char *token, const char *bucket, const char *org,
                const char *prefix);

/**
 * @brief Write current stats to InfluxDB
 *
 * @param client Pointer to the client structure
 * @return true if write successful
 * @return false if write failed
 */
bool influx_write(influx_client_t *client);

/**
 * @brief Deinitialize and cleanup the InfluxDB client
 *
 * @param client Pointer to the client structure
 */
void influx_deinit(influx_client_t *client);

#ifdef __cplusplus
}
#endif