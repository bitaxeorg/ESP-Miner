#ifndef BZM_BOARD_H
#define BZM_BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bzm/telemetry.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*bzm_dispatch_authorizer_t)(void * context);

#define BZM_BRINGUP_ASIC_COUNT BZM_MAX_ASIC_COUNT
#define BZM_BRINGUP_PLL_COUNT 2U

/* BZM startup and hardware state, independent of application power policy. */
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

#ifdef __cplusplus
}
#endif

#endif // BZM_BOARD_H
