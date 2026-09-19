#ifndef BZM_BRINGUP_H
#define BZM_BRINGUP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bzm_telemetry.h"
#include "bzm_topology.h"

#define BZM_BRINGUP_ASIC_COUNT BZM_MAX_ASIC_COUNT
#define BZM_BRINGUP_FIRST_ASIC_ID BZM_FIRST_ASIC_ID
#define BZM_BRINGUP_LAST_ASIC_ID BZM_LAST_ASIC_ID
#define BZM_BRINGUP_CONTROL_ENGINE_ID 0x0fffU
#define BZM_BRINGUP_PLL_COUNT 2U

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

typedef enum
{
    BZM_BRINGUP_PROBE_RESPONSE = 0,
    BZM_BRINGUP_PROBE_NO_RESPONSE,
    BZM_BRINGUP_PROBE_IO_ERROR,
} bzm_bringup_probe_result_t;

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

typedef struct
{
    bzm_bringup_probe_result_t (*probe_noop)(void * context, uint8_t asic_id);
    bool (*write_u32)(void * context, uint8_t asic_id, uint16_t engine_id, uint8_t offset, uint32_t value);
    bool (*read_u32)(void * context, uint8_t asic_id, uint16_t engine_id, uint8_t offset, uint32_t * value);
    void (*delay_ms)(void * context, uint32_t delay_ms);
    uint64_t (*now_us)(void * context);
    bool (*telemetry_snapshot)(void * context, bzm_telemetry_store_t * store, uint16_t timeout_ms);

    /*
     * The batch hooks and activation barrier are required for startup. The batch hooks
     * bracket one pair across all ASICs so the adapter can pause unsolicited
     * telemetry while collecting addressed acknowledgements. pair_commit
     * activates the higher-voltage stack first and must complete the other
     * stack before returning, limiting transient skew to one engine. The
     * barrier keeps ordinary mining dispatch closed until every sequential
     * pair is acknowledged.
     */
    bool (*balanced_batch_begin)(void * context, uint16_t pair_index);
    bool (*balanced_pair_commit)(void * context, uint8_t asic_id, const bzm_engine_pair_t * pair);
    bool (*balanced_batch_end)(void * context, uint16_t pair_index);
    bool (*activation_barrier)(void * context);
} bzm_bringup_ops_t;

void bzm_bringup_init(bzm_bringup_state_t * state);
uint32_t bzm_bringup_reference_tdm_control(void);

/* Configure the fixed Bonanza chain, then enable mining only after every
 * engine is active. Live tuning retains only running state and domain clocks. */
bzm_bringup_outcome_t bzm_bringup_start(bzm_bringup_state_t *state, const bzm_bringup_ops_t *ops,
                                       void *context, const bzm_bringup_telemetry_policy_t *policy,
                                       bzm_bringup_report_t *report);

const char * bzm_bringup_reason_name(bzm_bringup_reason_t reason);

/*
 * Apply one live tuning transaction across the eight ASIC/PLL
 * domains. TDM and mining remain active. Ordinary steps are limited to
 * 25 MHz per domain; allow_initial_jump is only for the bounded
 * target-minus-100 MHz shortcut after 800 MHz mining is proven.
 */
bzm_bringup_outcome_t bzm_bringup_live_frequency_domains_step(
    bzm_bringup_state_t *state, const bzm_bringup_ops_t *ops,
    void *ops_context,
    const float
        target_mhz[BZM_BRINGUP_ASIC_COUNT][BZM_BRINGUP_PLL_COUNT],
    bool allow_initial_jump, bzm_bringup_report_t *report);

#endif // BZM_BRINGUP_H
