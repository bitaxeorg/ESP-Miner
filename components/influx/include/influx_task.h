#ifndef INFLUX_TASK_H_
#define INFLUX_TASK_H_

#include <esp_err.h>
#include "../../main/global_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the InfluxDB client and start the stats reporting task
 *
 * This function will:
 * 1. Initialize the InfluxDB client with the provided configuration
 * 2. Test the connection to the InfluxDB server
 * 3. Create a task that periodically sends mining stats to InfluxDB
 *
 * @param state Pointer to the global state structure
 * @param host InfluxDB server hostname or IP
 * @param port InfluxDB server port
 * @param token Authentication token
 * @param bucket Bucket name
 * @param org Organization name
 * @param prefix Measurement prefix
 * @return bool true if initialization successful, false otherwise
 */
bool influx_init_and_start(GlobalState *state, const char *host, int port, const char *token, const char *bucket, const char *org, const char *prefix);

#ifdef __cplusplus
}
#endif

#endif /* INFLUX_TASK_H_ */