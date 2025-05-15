#ifndef INFLUX_TASK_H_
#define INFLUX_TASK_H_

#include "global_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Task that periodically sends mining stats to InfluxDB
 *
 * @param pvParameters Pointer to GlobalState structure
 */
void influx_task(void *pvParameters);

/**
 * @brief Initialize InfluxDB client
 *
 * This function will:
 * 1. Initialize the InfluxDB client with the provided configuration
 * 2. Test the connection to the InfluxDB server
 *
 * Note: Changes to InfluxDB configuration require a device restart to take effect.
 *
 * @param state Pointer to GlobalState structure
 * @param host InfluxDB server hostname
 * @param port InfluxDB server port
 * @param token Authentication token
 * @param bucket Bucket name
 * @param org Organization name
 * @param prefix Metric prefix
 * @return true if initialization successful
 * @return false if initialization failed
 */
bool influx_init_and_start(GlobalState *state, const char *host, int port,
                          const char *token, const char *bucket, const char *org,
                          const char *prefix);

#ifdef __cplusplus
}
#endif

#endif /* INFLUX_TASK_H_ */