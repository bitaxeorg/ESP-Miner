#ifndef BZM_REACTOR_H
#define BZM_REACTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "bzm/job_store.h"
#include "bzm/protocol.h"

typedef enum {
    BZM_ENGINE_IDLE = 0,
    BZM_ENGINE_ASSIGNED,
    BZM_ENGINE_FLUSHING,
} bzm_engine_state_t;

typedef enum {
    BZM_ASSIGN_OK = 0,
    BZM_ASSIGN_BUSY,
    BZM_ASSIGN_FLUSH_REQUIRED,
    BZM_ASSIGN_STORE_ERROR,
    BZM_ASSIGN_TRANSPORT_ERROR,
    BZM_ASSIGN_INVALID,
} bzm_assign_status_t;

typedef struct {
    bool (*write_work)(void *context, const bzm_work_t *work);
    /* Optional batch-dispatch checkpoint after each complete logical-engine
     * write. Hardware transports use this to bound an otherwise continuous
     * command burst and drain full-duplex receive traffic. Returning false
     * aborts and symmetrically flushes the incomplete generation. */
    bool (*dispatch_checkpoint)(void *context);
    bool (*flush)(void *context);
} bzm_transport_ops_t;

typedef struct {
    uint16_t engine_count;
    uint8_t timestamp_count;
    uint8_t lead_zeros;
    uint32_t nonce_offset;
} bzm_reactor_config_t;

typedef struct {
    bool active;
    uint16_t logical_engine_id;
    uint16_t engine_id;
    bzm_engine_state_t state;
    uint8_t logical_sequence;
    uint32_t epoch;
    bzm_work_handle_t handle;
    uint8_t timestamp_count;
    uint32_t base_ntime;
    uint32_t base_version;
    uint32_t nonce_offset;
    uint32_t versions[BZM_VERSION_VARIANTS];
} bzm_assignment_t;

typedef struct {
    bzm_job_store_t *job_store;
    bzm_transport_ops_t transport;
    void *transport_context;
    bzm_reactor_config_t config;
    bzm_assignment_t assignments[BZM_MAX_ACTIVE_WORK];
    /* The hardware may report the just-replaced job after the next job has
     * been programmed. Retain one prior generation independently per engine
     * instead of assuming every engine shares one batch identity. */
    bzm_assignment_t previous_assignments[BZM_MAX_ACTIVE_WORK];
    uint8_t next_engine_sequence[BZM_MAX_ACTIVE_WORK];
    uint16_t next_engine;
    uint32_t epoch;
    bool flush_pending;
    bool flush_complete;
    /* Hardware reset / sequence-reuse barrier only. Ordinary clean jobs
     * preserve wire identities and allow results from each updated engine. */
    bool results_quarantined;
} bzm_reactor_t;

bool bzm_reactor_init(bzm_reactor_t *reactor, bzm_job_store_t *job_store,
                      const bzm_reactor_config_t *config,
                      const bzm_transport_ops_t *transport,
                      void *transport_context);

bzm_assign_status_t bzm_reactor_assign(bzm_reactor_t *reactor,
                                       const asic_job_t *template,
                                       bzm_work_t *assigned_work);

bool bzm_reactor_begin_flush(bzm_reactor_t *reactor);
void bzm_reactor_finish_flush(bzm_reactor_t *reactor);
bool bzm_reactor_is_flush_pending(const bzm_reactor_t *reactor);
bool bzm_reactor_results_quarantined(const bzm_reactor_t *reactor);
// Retire pool ownership while preserving balanced scheduling and wire IDs.
bool bzm_reactor_invalidate_work(bzm_reactor_t *reactor);
bool bzm_reactor_map_result(bzm_reactor_t *reactor,
                            const bzm_raw_result_t *raw,
                            bzm_result_t *result);

#endif // BZM_REACTOR_H
