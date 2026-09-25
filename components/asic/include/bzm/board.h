#ifndef BZM_BOARD_H
#define BZM_BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bzm/telemetry.h"

typedef struct GlobalState GlobalState;
typedef bool (*bzm_dispatch_authorizer_t)(void * context);

#define BZM_BRINGUP_ASIC_COUNT BZM_MAX_ASIC_COUNT
#define BZM_BRINGUP_PLL_COUNT 2U

/* Driver snapshots and controls used by the Bonanza board owner. */
typedef enum
{
    BZM_BRINGUP_GOOD = 0,
    BZM_BRINGUP_BAD,
} bzm_bringup_outcome_t;

typedef enum
{
    BZM_BRINGUP_REASON_NONE = 0,
    BZM_BRINGUP_REASON_INVALID_ARGUMENT,
    BZM_BRINGUP_REASON_PREREQUISITE,
    BZM_BRINGUP_REASON_IO,
    BZM_BRINGUP_REASON_CHAIN_MISSING,
    BZM_BRINGUP_REASON_CHAIN_ID_MISMATCH,
    BZM_BRINGUP_REASON_CHAIN_EXTRA_ASIC,
    BZM_BRINGUP_REASON_REGISTER_READBACK,
    BZM_BRINGUP_REASON_TELEMETRY_MISSING,
    BZM_BRINGUP_REASON_TELEMETRY_PRECONFIG,
    BZM_BRINGUP_REASON_TELEMETRY_STALE,
    BZM_BRINGUP_REASON_TELEMETRY_UNSAFE,
    BZM_BRINGUP_REASON_PLL_UNLOCKED,
    BZM_BRINGUP_REASON_TOPOLOGY,
    BZM_BRINGUP_REASON_BALANCED_PAIR_COMMIT,
    BZM_BRINGUP_REASON_ACTIVATION_BARRIER,
    BZM_BRINGUP_REASON_BALANCED_BATCH,
} bzm_bringup_reason_t;

typedef struct
{
    bzm_bringup_reason_t reason;
    uint8_t asic_id;
    uint8_t pll_index;
    uint8_t register_offset;
    uint32_t expected;
    uint32_t actual;
} bzm_bringup_report_t;

typedef struct
{
    bool running;
    float clock_mhz;
    float domain_clock_mhz[BZM_BRINGUP_ASIC_COUNT][BZM_BRINGUP_PLL_COUNT];
} bzm_bringup_state_t;

typedef struct
{
    bzm_telemetry_bounds_t bounds;
    uint64_t max_age_us;
    /* CH2 excursions, including a voltage-fault bit in the same unchecksummed
     * frame, require this many consecutive fresh frames. A value of one
     * preserves immediate fail-closed behavior. */
    uint8_t ch2_confirm_samples;
} bzm_bringup_telemetry_policy_t;

const char * bzm_bringup_reason_name(bzm_bringup_reason_t reason);

/* Asynchronous transport failures are consumed by the board power owner. */
bool BZM_has_io_fault(void);
bool BZM_work_replacement_snapshot(uint32_t *generation, uint32_t *completed, bool *pending);

/* Thread-safe copies of the singleton transport's telemetry. */
bool BZM_get_telemetry_snapshot(bzm_telemetry_store_t * snapshot);
/* Complete hardware startup under the driver lock. Dispatch stays closed
 * until the board owner installs its authorization callback after success. */
bzm_bringup_outcome_t BZM_start(GlobalState *state, const bzm_bringup_telemetry_policy_t *policy,
                                 bzm_bringup_report_t *report);
bzm_bringup_outcome_t BZM_step_frequency_domains(
    const float
        target_mhz[BZM_BRINGUP_ASIC_COUNT][BZM_BRINGUP_PLL_COUNT],
    bool allow_initial_jump, bzm_bringup_report_t *report,
    float *actual_mhz);
bool BZM_get_state(bzm_bringup_state_t * state);
bool BZM_hold_reset(void);
/* The callback is evaluated before dispatch and before every engine write.
 * It must be non-blocking and must not call back into this driver. NULL is
 * fail-closed. */
void BZM_set_dispatch_authorizer(bzm_dispatch_authorizer_t authorize, void * context);
/* Evaluated before every startup bridge/UART operation, including while
 * startup holds the board mutex, to enforce cancellation and its deadline. */
void BZM_set_operation_authorizer(bzm_dispatch_authorizer_t authorize, void * context);
/* Pump the singleton parser for health monitoring; returns emitted frames. */
size_t BZM_poll(uint16_t timeout_ms);

#endif // BZM_BOARD_H
