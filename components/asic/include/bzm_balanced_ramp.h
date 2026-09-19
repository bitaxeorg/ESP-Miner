#ifndef BZM_BALANCED_RAMP_H
#define BZM_BALANCED_RAMP_H

#include <stdbool.h>
#include <stdint.h>

#include "bzm_bringup.h"
#include "bzm_transport.h"

#define BZM_BALANCED_RAMP_ENGINE_CONFIG 0x04U
#define BZM_BALANCED_RAMP_ENGINE_BUSY_MASK 0x01U
/* Bits 0, 1, 2, 5, and 6 are the reference firmware's writable TCE/mode
 * controls. Bit 4 is hardware-owned and asserts in the active readback. */
#define BZM_BALANCED_RAMP_ENGINE_CONFIG_WRITABLE_MASK 0x67U
#define BZM_BALANCED_RAMP_ENGINE_CONFIG_ACTIVE_MASK 0x10U
#define BZM_BALANCED_RAMP_ENGINE_CONFIG_ALLOWED_MASK \
    (BZM_BALANCED_RAMP_ENGINE_CONFIG_WRITABLE_MASK | BZM_BALANCED_RAMP_ENGINE_CONFIG_ACTIVE_MASK)

typedef enum
{
    BZM_BALANCED_RAMP_FAILURE_NONE = 0,
    BZM_BALANCED_RAMP_FAILURE_ARGUMENT,
    BZM_BALANCED_RAMP_FAILURE_TELEMETRY,
    BZM_BALANCED_RAMP_FAILURE_LEASE,
    BZM_BALANCED_RAMP_FAILURE_ENGINE_RESET,
    BZM_BALANCED_RAMP_FAILURE_CONFIG_WRITE,
    BZM_BALANCED_RAMP_FAILURE_SENTINEL_WRITE,
    BZM_BALANCED_RAMP_FAILURE_STATUS_READ,
    BZM_BALANCED_RAMP_FAILURE_NOT_BUSY,
    BZM_BALANCED_RAMP_FAILURE_CONFIG_READBACK,
} bzm_balanced_ramp_failure_t;

typedef struct
{
    bzm_balanced_ramp_failure_t failure;
    uint8_t failure_asic_id;
    uint16_t failure_engine_id;
    uint8_t failure_register_offset;
    uint32_t failure_expected;
    uint32_t failure_actual;
} bzm_balanced_ramp_report_t;

typedef struct
{
    /* Recheck bounded execution authorization immediately before each engine
     * is activated. The enclosing balanced-batch hooks own the external
     * safety-lease heartbeat. */
    bool (*begin_engine)(void * context, uint8_t asic_id, uint16_t engine_id);
    bool (*write_register)(void * context, uint8_t asic_id, uint16_t engine_id, uint8_t offset, const void * data,
                           size_t data_len);
    bool (*read_register)(void * context, uint8_t asic_id, uint16_t engine_id, uint8_t offset, void * data,
                          size_t data_len);
    void (*delay_ms)(void * context, uint32_t delay_ms);
    bool (*telemetry_sample)(void * context, uint8_t asic_id, bzm_telemetry_sample_t * sample);
} bzm_balanced_ramp_ops_t;

const char * bzm_balanced_ramp_failure_name(bzm_balanced_ramp_failure_t failure);

/*
 * Activate one bottom/top pair using the BIRDS reference ordering rule: the
 * higher-voltage stack is activated first, followed immediately by the other
 * stack. Each engine receives deterministic no-result sentinel work and must
 * acknowledge busy + enhanced-mode config before the next engine starts.
 * The startup loop calls each pair exactly once in ascending order and aborts
 * immediately on failure. Pair zero resets the ASIC's engine domain.
 * The only permitted transient imbalance is the first member of this pair.
 */
bool bzm_balanced_ramp_commit_pair(bzm_balanced_ramp_report_t * ramp, const bzm_balanced_ramp_ops_t * ops, void * ops_context,
                                   uint8_t asic_id, const bzm_engine_pair_t * pair);

#endif // BZM_BALANCED_RAMP_H
