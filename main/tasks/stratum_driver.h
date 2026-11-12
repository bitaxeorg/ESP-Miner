#ifndef STRATUM_DRIVER_H_
#define STRATUM_DRIVER_H_

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include "asic_job.h"
#include "device_config.h"

typedef struct GlobalState GlobalState;

typedef struct stratum_driver {
    stratum_protocol_t protocol;
    const char *name;
    esp_err_t (*run)(GlobalState *GLOBAL_STATE, uint16_t pool_idx);
    void (*close_connection)(GlobalState *GLOBAL_STATE, uint16_t pool_idx);
    void (*abort_connection)(uint16_t pool_idx);
    int (*submit_share)(GlobalState *GLOBAL_STATE, const asic_job_t *active_job, uint32_t nonce, uint32_t rolled_version, uint64_t *sent_time_us);
} stratum_driver_t;

extern const stratum_driver_t g_stratum_v1_driver;
extern const stratum_driver_t g_stratum_v2_driver;

static inline const stratum_driver_t *stratum_get_driver(stratum_protocol_t protocol)
{
    if (protocol == STRATUM_PROTOCOL_V1) {
        return &g_stratum_v1_driver;
    }
    if (protocol == STRATUM_PROTOCOL_V2) {
        return &g_stratum_v2_driver;
    }
    return NULL;
}

static inline const stratum_driver_t *stratum_get_driver_for_job(const asic_job_t *job)
{
    if (!job) {
        return NULL;
    }
    if (job->source_type == JOB_TYPE_SV2_STANDARD || job->source_type == JOB_TYPE_SV2_EXTENDED) {
        return &g_stratum_v2_driver;
    }
    return &g_stratum_v1_driver;
}

#endif // STRATUM_DRIVER_H_
