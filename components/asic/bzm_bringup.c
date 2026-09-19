#include "bzm_bringup.h"

#include <math.h>
#include <stddef.h>

#include "bzm.h"
#include "bzm_frequency.h"
#include "bzm_registers.h"

enum
{
    BZM_REFERENCE_SLOW_CLOCK_DIVIDER = 2,
    BZM_REFERENCE_TDM_SLOT_BIT_COUNT = 0x7f,
    BZM_REFERENCE_TDM_SLOT_COUNT = 100,
    BZM_REFERENCE_TDM_DELAY = 1,
    BZM_REFERENCE_SENSOR_DIVIDER = 8,
    /* About 6.2 reports per second per ASIC, within the 2-second age limit. */
    BZM_REFERENCE_SENSOR_TDM_GAP_COUNT = 63,
    BZM_REFERENCE_THRESHOLD_COUNT = 10,
    BZM_RESULT_REPORT_ENABLED = 0,
    BZM_RESULT_REPORT_DISABLED = 1,
    BZM_REFERENCE_THERMAL_TRIP_CODE_115C = 2650,
    BZM_REFERENCE_VOLTAGE_SHUTDOWN_CODE_500MV = 7561,
    BZM_REFERENCE_SENSOR_SETTLE_MS = 10,
    BZM_CHAIN_NOOP_ATTEMPTS = 5,
    BZM_CHAIN_NOOP_RETRY_DELAY_MS = 200,
    BZM_TELEMETRY_ACQUIRE_ATTEMPTS = 5,
    BZM_TELEMETRY_RETRY_DELAY_MS = 5,
    BZM_PLL_COUNT = 2,
    BZM_PLL_800_FBDIV = 128,
    BZM_PLL_800_POSTDIV = 0x1242,
    BZM_PLL_LOCK_ATTEMPTS = 30,
    BZM_PLL_LOCK_POLL_MS = 100,
    BZM_PLL_LOCK_MASK = 0x05,
    BZM_PLL_ENABLE_VALUE = 0x01,
};

static const uint32_t BZM_REFERENCE_BIRDS_IO_PEPS_DRIVE_STRENGTH = 0x44464444U;

typedef struct
{
    uint8_t offset;
    uint32_t value;
} bzm_register_value_t;

static bzm_bringup_outcome_t set_report(bzm_bringup_report_t * report, bzm_bringup_reason_t reason,
                                        uint8_t asic_id, uint8_t pll_index, uint8_t register_offset, uint32_t expected,
                                        uint32_t actual)
{
    if (report != NULL) {
        *report = (bzm_bringup_report_t){
            .reason = reason,
            .asic_id = asic_id,
            .pll_index = pll_index,
            .register_offset = register_offset,
            .expected = expected,
            .actual = actual,
        };
    }
    return reason == BZM_BRINGUP_REASON_NONE ? BZM_BRINGUP_GOOD : BZM_BRINGUP_BAD;
}

void bzm_bringup_init(bzm_bringup_state_t * state)
{
    if (state != NULL) {
        *state = (bzm_bringup_state_t){0};
    }
}

uint32_t bzm_bringup_reference_tdm_control(void)
{
    return ((uint32_t) BZM_REFERENCE_TDM_SLOT_BIT_COUNT << 9) | ((uint32_t) BZM_REFERENCE_TDM_SLOT_COUNT << 1) | 1U;
}

const char * bzm_bringup_reason_name(bzm_bringup_reason_t reason)
{
    switch (reason) {
    case BZM_BRINGUP_REASON_NONE:
        return "none";
    case BZM_BRINGUP_REASON_INVALID_ARGUMENT:
        return "invalid_argument";
    case BZM_BRINGUP_REASON_PREREQUISITE:
        return "prerequisite";
    case BZM_BRINGUP_REASON_IO:
        return "io";
    case BZM_BRINGUP_REASON_CHAIN_MISSING:
        return "chain_missing";
    case BZM_BRINGUP_REASON_CHAIN_ID_MISMATCH:
        return "chain_id_mismatch";
    case BZM_BRINGUP_REASON_CHAIN_EXTRA_ASIC:
        return "chain_extra_asic";
    case BZM_BRINGUP_REASON_REGISTER_READBACK:
        return "register_readback";
    case BZM_BRINGUP_REASON_TELEMETRY_MISSING:
        return "telemetry_missing";
    case BZM_BRINGUP_REASON_TELEMETRY_PRECONFIG:
        return "telemetry_preconfig";
    case BZM_BRINGUP_REASON_TELEMETRY_STALE:
        return "telemetry_stale";
    case BZM_BRINGUP_REASON_TELEMETRY_UNSAFE:
        return "telemetry_unsafe";
    case BZM_BRINGUP_REASON_PLL_UNLOCKED:
        return "pll_unlocked";
    case BZM_BRINGUP_REASON_TOPOLOGY:
        return "topology";
    case BZM_BRINGUP_REASON_BALANCED_PAIR_COMMIT:
        return "balanced_pair_commit";
    case BZM_BRINGUP_REASON_ACTIVATION_BARRIER:
        return "activation_barrier";
    case BZM_BRINGUP_REASON_BALANCED_BATCH:
        return "balanced_batch";
    default:
        return "unknown";
    }
}

static bool basic_ops_are_valid(const bzm_bringup_ops_t * ops)
{
    return ops != NULL && ops->write_u32 != NULL && ops->read_u32 != NULL;
}

static bool telemetry_policy_is_valid(const bzm_bringup_telemetry_policy_t * policy)
{
    return policy != NULL && policy->max_age_us != 0 && policy->ch2_confirm_samples != 0 &&
           policy->ch2_confirm_samples <= BZM_CH2_CONFIRM_MAX_SAMPLES && isfinite(policy->bounds.temperature_min_c) &&
           isfinite(policy->bounds.temperature_max_c) && isfinite(policy->bounds.ch0_min_mv) &&
           isfinite(policy->bounds.ch0_max_mv) && isfinite(policy->bounds.ch1_min_mv) && isfinite(policy->bounds.ch1_max_mv) &&
           isfinite(policy->bounds.ch2_abs_max_mv) && isfinite(policy->bounds.max_stack_spread_mv) &&
           policy->bounds.temperature_min_c <= policy->bounds.temperature_max_c &&
           policy->bounds.ch0_min_mv <= policy->bounds.ch0_max_mv && policy->bounds.ch1_min_mv <= policy->bounds.ch1_max_mv &&
           policy->bounds.ch2_abs_max_mv >= 0.0f && policy->bounds.max_stack_spread_mv >= 0.0f;
}

static bzm_bringup_outcome_t bzm_bringup_discover_chain(const bzm_bringup_ops_t * ops, void * ops_context,
                                               bzm_bringup_report_t * report)
{
    for (uint8_t index = 0; index < BZM_BRINGUP_ASIC_COUNT; ++index) {
        uint8_t asic_id = bzm_asic_wire_ids[index];
        bzm_bringup_probe_result_t probe = BZM_BRINGUP_PROBE_NO_RESPONSE;
        for (uint8_t attempt = 0; attempt < BZM_CHAIN_NOOP_ATTEMPTS; ++attempt) {
            probe = ops->probe_noop(ops_context, BZM_BROADCAST_ASIC);
            if (probe != BZM_BRINGUP_PROBE_NO_RESPONSE) {
                break;
            }
            /* BIRDS retries the default-address power-up NOOP because an ASIC
             * may not answer the first request after reset release. Keep this
             * bounded well below its 30-attempt reference allowance. A
             * malformed frame/I/O error is never retried. */
            if (attempt + 1U < BZM_CHAIN_NOOP_ATTEMPTS) {
                ops->delay_ms(ops_context, BZM_CHAIN_NOOP_RETRY_DELAY_MS);
            }
        }
        if (probe != BZM_BRINGUP_PROBE_RESPONSE) {
            bzm_bringup_reason_t reason =
                probe == BZM_BRINGUP_PROBE_NO_RESPONSE ? BZM_BRINGUP_REASON_CHAIN_MISSING : BZM_BRINGUP_REASON_IO;
            return set_report(report, reason, asic_id, 0, BZM_LOCAL_REG_ASIC_ID, BZM_BRINGUP_PROBE_RESPONSE, probe);
        }

        uint32_t programmed_id = asic_id | (index == 0 ? 0U : (1U << 8));
        if (!ops->write_u32(ops_context, BZM_BROADCAST_ASIC, BZM_BRINGUP_CONTROL_ENGINE_ID, BZM_LOCAL_REG_ASIC_ID, programmed_id)) {
            return set_report(report, BZM_BRINGUP_REASON_IO, asic_id, 0, BZM_LOCAL_REG_ASIC_ID, programmed_id, 0);
        }
        ops->delay_ms(ops_context, 200);

        uint32_t readback = 0;
        uint32_t expected_readback = asic_id | (1U << 8);
        if (!ops->read_u32(ops_context, asic_id, BZM_BRINGUP_CONTROL_ENGINE_ID, BZM_LOCAL_REG_ASIC_ID, &readback)) {
            return set_report(report, BZM_BRINGUP_REASON_IO, asic_id, 0, BZM_LOCAL_REG_ASIC_ID, expected_readback,
                              0);
        }
        if (readback != expected_readback) {
            return set_report(report, BZM_BRINGUP_REASON_CHAIN_ID_MISMATCH, asic_id, 0, BZM_LOCAL_REG_ASIC_ID,
                              expected_readback, readback);
        }

        /* BIRDS uses NOOP only at the default/unassigned address during
         * discovery. The exact addressed register readback above proves the
         * newly assigned ASIC; an addressed NOOP is not a supported identity
         * check on this chain. */
    }

    bzm_bringup_probe_result_t extra_probe = ops->probe_noop(ops_context, BZM_BROADCAST_ASIC);
    if (extra_probe == BZM_BRINGUP_PROBE_RESPONSE) {
        return set_report(report, BZM_BRINGUP_REASON_CHAIN_EXTRA_ASIC, BZM_BROADCAST_ASIC, 0,
                          BZM_LOCAL_REG_ASIC_ID, BZM_BRINGUP_PROBE_NO_RESPONSE, extra_probe);
    }
    if (extra_probe == BZM_BRINGUP_PROBE_IO_ERROR) {
        return set_report(report, BZM_BRINGUP_REASON_IO, BZM_BROADCAST_ASIC, 0, BZM_LOCAL_REG_ASIC_ID,
                          BZM_BRINGUP_PROBE_NO_RESPONSE, extra_probe);
    }

    return set_report(report, BZM_BRINGUP_REASON_NONE, BZM_LAST_ASIC_ID, 0, BZM_LOCAL_REG_ASIC_ID,
                      BZM_BRINGUP_ASIC_COUNT, BZM_BRINGUP_ASIC_COUNT);
}

static bzm_bringup_outcome_t write_and_verify_registers(const bzm_bringup_ops_t * ops, void * ops_context, uint8_t asic_id,
                                                        const bzm_register_value_t * registers, size_t register_count, bzm_bringup_report_t * report)
{
    for (size_t i = 0; i < register_count; ++i) {
        if (!ops->write_u32(ops_context, asic_id, BZM_BRINGUP_CONTROL_ENGINE_ID, registers[i].offset, registers[i].value)) {
            return set_report(report, BZM_BRINGUP_REASON_IO, asic_id, 0, registers[i].offset, registers[i].value,
                              0);
        }
    }
    for (size_t i = 0; i < register_count; ++i) {
        uint32_t actual = 0;
        if (!ops->read_u32(ops_context, asic_id, BZM_BRINGUP_CONTROL_ENGINE_ID, registers[i].offset, &actual)) {
            return set_report(report, BZM_BRINGUP_REASON_IO, asic_id, 0, registers[i].offset, registers[i].value,
                              0);
        }
        if (actual != registers[i].value) {
            return set_report(report, BZM_BRINGUP_REASON_REGISTER_READBACK, asic_id, 0, registers[i].offset,
                              registers[i].value, actual);
        }
    }
    return BZM_BRINGUP_GOOD;
}

static bzm_bringup_outcome_t verify_telemetry_snapshot(const bzm_telemetry_store_t * store, uint64_t now_us,
                                                       uint64_t configured_after_us, const bzm_bringup_telemetry_policy_t * policy,
                                                       bool require_clock_locks, bzm_bringup_report_t * report)
{
    for (uint8_t index = 0; index < BZM_BRINGUP_ASIC_COUNT; ++index) {
        uint8_t asic_id = bzm_asic_wire_ids[index];
        const bzm_telemetry_sample_t * sample = bzm_telemetry_store_get(store, asic_id);
        if (sample == NULL || !sample->received) {
            return set_report(report, BZM_BRINGUP_REASON_TELEMETRY_MISSING, asic_id, 0, 0, 1, 0);
        }
        if (sample->timestamp_us < configured_after_us) {
            return set_report(report, BZM_BRINGUP_REASON_TELEMETRY_PRECONFIG, asic_id, 0, 0,
                              (uint32_t) configured_after_us, (uint32_t) sample->timestamp_us);
        }
        if (!bzm_telemetry_sample_is_fresh(sample, now_us, policy->max_age_us)) {
            uint64_t age_us = now_us >= sample->timestamp_us ? now_us - sample->timestamp_us : UINT64_MAX;
            return set_report(report, BZM_BRINGUP_REASON_TELEMETRY_STALE, asic_id, 0, 0,
                              (uint32_t) policy->max_age_us, (uint32_t) age_us);
        }
        /* Only a finite CH2 value outside its absolute bound is eligible for
         * confirmation. A voltage-fault bit in that same unchecksummed frame
         * is qualified with the CH2 excursion. Trips, every other invalid
         * state, clock loss, temperature, CH0, CH1, and stack-spread
         * violations remain immediate failures. */
        if (!bzm_telemetry_sample_is_safe_except_ch2(sample, now_us, policy->max_age_us, &policy->bounds, require_clock_locks)) {
            return set_report(report, BZM_BRINGUP_REASON_TELEMETRY_UNSAFE, asic_id, 0, 0, 1, 0);
        }
    }
    return BZM_BRINGUP_GOOD;
}

static bzm_bringup_outcome_t verify_telemetry(const bzm_bringup_ops_t * ops, void * ops_context,
                                              const bzm_bringup_telemetry_policy_t * policy, uint64_t configured_after_us,
                                              bool require_clock_locks, uint16_t timeout_ms, bzm_bringup_report_t * report)
{
    bzm_telemetry_store_t store = {0};
    bool all_post_configuration = false;
    for (uint8_t attempt = 0; attempt < BZM_TELEMETRY_ACQUIRE_ATTEMPTS; ++attempt) {
        if (!ops->telemetry_snapshot(ops_context, &store, timeout_ms)) {
            return set_report(report, BZM_BRINGUP_REASON_IO, 0, 0, 0, 0, 0);
        }

        all_post_configuration = true;
        for (uint8_t index = 0; index < BZM_BRINGUP_ASIC_COUNT; ++index) {
            uint8_t asic_id = bzm_asic_wire_ids[index];
            const bzm_telemetry_sample_t * sample = bzm_telemetry_store_get(&store, asic_id);
            if (sample == NULL || !sample->received || sample->timestamp_us < configured_after_us) {
                all_post_configuration = false;
                break;
            }
        }
        if (all_post_configuration) {
            break;
        }
        if (attempt + 1U < BZM_TELEMETRY_ACQUIRE_ATTEMPTS) {
            ops->delay_ms(ops_context, BZM_TELEMETRY_RETRY_DELAY_MS);
        }
    }

    bzm_telemetry_confirmation_t confirmation;
    bzm_telemetry_confirmation_init(&confirmation);
    uint8_t culprit_asic_id = 0;
    uint8_t observed_samples = 0;
    uint16_t attempt_limit = (uint16_t) policy->ch2_confirm_samples * BZM_BRINGUP_ASIC_COUNT;
    for (uint16_t attempt = 0; attempt <= attempt_limit; ++attempt) {
        uint64_t snapshot_now_us = ops->now_us(ops_context);
        bzm_bringup_outcome_t outcome =
            verify_telemetry_snapshot(&store, snapshot_now_us, configured_after_us, policy, require_clock_locks, report);

        /* CH2 excursions already pass the strict non-CH2 checks above. A
         * combined-PLL bit loss is also confirmable when every non-clock
         * field is otherwise safe. Missing, stale, pre-configuration,
         * malformed, and all other unsafe samples remain immediate failures. */
        bool confirmable = outcome == BZM_BRINGUP_GOOD;
        if (!confirmable && outcome == BZM_BRINGUP_BAD && report != NULL && report->reason == BZM_BRINGUP_REASON_TELEMETRY_UNSAFE &&
            require_clock_locks) {
            const bzm_telemetry_sample_t * sample = bzm_telemetry_store_get(&store, report->asic_id);
            confirmable =
                sample != NULL && !sample->pll_locked && !bzm_telemetry_sample_has_immediate_trip(sample) &&
                bzm_telemetry_sample_is_safe_except_ch2(sample, snapshot_now_us, policy->max_age_us, &policy->bounds, false);
        }
        if (!confirmable)
            return outcome;

        bzm_ch2_confirmation_result_t confirmation_result = bzm_telemetry_confirmation_observe(
            &confirmation, &store, snapshot_now_us, policy->max_age_us, &policy->bounds, require_clock_locks,
            policy->ch2_confirm_samples, &culprit_asic_id, &observed_samples);
        if (confirmation_result == BZM_CH2_CONFIRMATION_GOOD)
            return BZM_BRINGUP_GOOD;
        if (confirmation_result == BZM_CH2_CONFIRMATION_CONTINUOUS || confirmation_result == BZM_CH2_CONFIRMATION_INVALID) {
            return set_report(report, BZM_BRINGUP_REASON_TELEMETRY_UNSAFE, culprit_asic_id, 0, 0,
                              policy->ch2_confirm_samples, observed_samples);
        }
        if (attempt == attempt_limit)
            break;

        /* Require recovery or the configured number of consecutive fresh
         * same-ASIC anomalies. The four-ASIC attempt multiplier gives each
         * TDM slot a bounded chance to produce a newer sample without letting
         * migrating one-off corruption hold powered validation indefinitely. */
        ops->delay_ms(ops_context, BZM_TELEMETRY_RETRY_DELAY_MS);
        if (!ops->telemetry_snapshot(ops_context, &store, timeout_ms)) {
            return set_report(report, BZM_BRINGUP_REASON_IO, culprit_asic_id, 0, 0, policy->ch2_confirm_samples,
                              observed_samples);
        }
    }

    return set_report(report, BZM_BRINGUP_REASON_TELEMETRY_UNSAFE, culprit_asic_id, 0, 0,
                      policy->ch2_confirm_samples, observed_samples);
}

static bzm_bringup_outcome_t bzm_bringup_configure_sensors(const bzm_bringup_ops_t * ops, void * ops_context,
                                                const bzm_bringup_telemetry_policy_t * telemetry_policy,
                                                bzm_bringup_report_t * report)
{
    const uint32_t tdm_control = bzm_bringup_reference_tdm_control();
    for (uint8_t index = 0; index < BZM_BRINGUP_ASIC_COUNT; ++index) {
        uint8_t asic_id = bzm_asic_wire_ids[index];
        uint32_t bandgap = 0;
        if (!ops->read_u32(ops_context, asic_id, BZM_BRINGUP_CONTROL_ENGINE_ID, BZM_LOCAL_REG_BANDGAP, &bandgap)) {
            return set_report(report, BZM_BRINGUP_REASON_IO, asic_id, 0, BZM_LOCAL_REG_BANDGAP, 0, 0);
        }

        const bzm_register_value_t registers[] = {
            /* Quiesce unsolicited traffic before any control readback. */
            {BZM_LOCAL_REG_UART_TDM_CONTROL, tdm_control & ~1U},
            {BZM_LOCAL_REG_UART_TX, 0x0f},
            {BZM_LOCAL_REG_SLOW_CLOCK_DIVIDER, BZM_REFERENCE_SLOW_CLOCK_DIVIDER},
            {BZM_LOCAL_REG_TDM_DELAY, BZM_REFERENCE_TDM_DELAY},
            {BZM_LOCAL_REG_SENSOR_CLOCK_DIVIDER, ((uint32_t) BZM_REFERENCE_SENSOR_DIVIDER << 5) | BZM_REFERENCE_SENSOR_DIVIDER},
            {BZM_LOCAL_REG_DTS_RESET_POWERDOWN, 1U << 8},
            {BZM_LOCAL_REG_SENSOR_TDM_GAP_COUNT, BZM_REFERENCE_SENSOR_TDM_GAP_COUNT},
            {BZM_LOCAL_REG_DTS_CONFIG, 0},
            {BZM_LOCAL_REG_SENSOR_THRESHOLD_COUNT,
             ((uint32_t) BZM_REFERENCE_THRESHOLD_COUNT << 16) | BZM_REFERENCE_THRESHOLD_COUNT},
            {BZM_LOCAL_REG_TEMPERATURE_TUNE_CODE, 0x8001U | ((uint32_t) BZM_REFERENCE_THERMAL_TRIP_CODE_115C << 1)},
            {BZM_LOCAL_REG_BANDGAP, (bandgap & ~0x0fU) | 0x03U},
            {BZM_LOCAL_REG_VSENSOR_RESET_POWERDOWN, 1U << 8},
            {BZM_LOCAL_REG_VSENSOR_CONFIG, (8U << 28) | (1U << 24)},
            {BZM_LOCAL_REG_VSENSOR_CONTROL,
             ((uint32_t) BZM_REFERENCE_VOLTAGE_SHUTDOWN_CODE_500MV << 16) | ((uint32_t) BZM_REFERENCE_VOLTAGE_SHUTDOWN_CODE_500MV << 1) | 1U},
        };
        bzm_bringup_outcome_t outcome = write_and_verify_registers(ops, ops_context, asic_id, registers,
                                                                   sizeof(registers) / sizeof(registers[0]), report);
        if (outcome != BZM_BRINGUP_GOOD) {
            return outcome;
        }
    }

    /* BIRDS drives the shared chain from the first ASIC in its only stack.
     * Its production sequence programs this exact value specifically to fix
     * UART unknown-message corruption. Prove it before unsolicited TDM
     * traffic begins so the parser barrier validates a conditioned link. */
    const bzm_register_value_t drive_strength[] = {
        {BZM_LOCAL_REG_IO_PEPS_DRIVE_STRENGTH, BZM_REFERENCE_BIRDS_IO_PEPS_DRIVE_STRENGTH},
    };
    bzm_bringup_outcome_t drive_outcome = write_and_verify_registers(ops, ops_context, BZM_FIRST_ASIC_ID, drive_strength,
                                                                     sizeof(drive_strength) / sizeof(drive_strength[0]), report);
    if (drive_outcome != BZM_BRINGUP_GOOD) {
        return drive_outcome;
    }

    /* UART_TX only selects which packet classes may be transmitted. TDM is
     * a separate enable in register 0x07. Start all four transmitters with a
     * single all-ASIC write only after every other sensor control has read
     * back. Besides matching the BIRDS reference sequence, one shared epoch
     * preserves the intentional ten-slot spacing between wire IDs instead of
     * creating four separately phased schedules. The slot count deliberately
     * exceeds the highest wire ID (0x28); a count of four would only allocate
     * slots for IDs 0..3 and these ASICs would never win a transmit slot. */
    if (!ops->write_u32(ops_context, BZM_ALL_ASICS, BZM_BRINGUP_CONTROL_ENGINE_ID, BZM_LOCAL_REG_UART_TDM_CONTROL, tdm_control)) {
        return set_report(report, BZM_BRINGUP_REASON_IO, BZM_ALL_ASICS, 0, BZM_LOCAL_REG_UART_TDM_CONTROL,
                          tdm_control, 0);
    }

    uint64_t sensors_configured_us = ops->now_us(ops_context);
    ops->delay_ms(ops_context, BZM_REFERENCE_SENSOR_SETTLE_MS);
    bzm_bringup_outcome_t outcome =
        verify_telemetry(ops, ops_context, telemetry_policy, sensors_configured_us, false, 100, report);
    if (outcome != BZM_BRINGUP_GOOD) {
        return outcome;
    }

    return set_report(report, BZM_BRINGUP_REASON_NONE, BZM_LAST_ASIC_ID, 0, 0, BZM_BRINGUP_ASIC_COUNT,
                      BZM_BRINGUP_ASIC_COUNT);
}

static uint8_t pll_register(uint8_t pll, uint8_t pll0_register)
{
    return pll == 0 ? pll0_register : (uint8_t) (pll0_register + 0x0aU);
}

static void set_all_domain_clocks(bzm_bringup_state_t *state, float mhz)
{
    for (size_t asic = 0; asic < BZM_BRINGUP_ASIC_COUNT; ++asic) {
        for (size_t pll = 0; pll < BZM_BRINGUP_PLL_COUNT; ++pll) {
            state->domain_clock_mhz[asic][pll] = mhz;
        }
    }
    state->clock_mhz = mhz;
}

static float average_domain_clocks(const bzm_bringup_state_t *state)
{
    float total = 0.0f;
    for (size_t asic = 0; asic < BZM_BRINGUP_ASIC_COUNT; ++asic) {
        for (size_t pll = 0; pll < BZM_BRINGUP_PLL_COUNT; ++pll) {
            total += state->domain_clock_mhz[asic][pll];
        }
    }
    return total /
        (float)(BZM_BRINGUP_ASIC_COUNT * BZM_BRINGUP_PLL_COUNT);
}

static bzm_bringup_outcome_t bzm_bringup_configure_clocks(bzm_bringup_state_t * state, const bzm_bringup_ops_t * ops, void * ops_context,
                                               const bzm_bringup_telemetry_policy_t * telemetry_policy,
                                               bzm_bringup_report_t * report)
{
    const uint32_t tdm_control = bzm_bringup_reference_tdm_control();
    /* Stop unsolicited telemetry on every ASIC before reading any control
     * register. Otherwise early ASICs can fill the UART while a later ASIC's
     * reply is still waiting for its TDM slot. */
    if (!ops->write_u32(ops_context, BZM_ALL_ASICS, BZM_BRINGUP_CONTROL_ENGINE_ID, BZM_LOCAL_REG_UART_TDM_CONTROL,
                        tdm_control & ~1U)) {
        return set_report(report, BZM_BRINGUP_REASON_IO, BZM_ALL_ASICS, 0, BZM_LOCAL_REG_UART_TDM_CONTROL,
                          tdm_control & ~1U, 0);
    }
    for (uint8_t index = 0; index < BZM_BRINGUP_ASIC_COUNT; ++index) {
        uint8_t asic_id = bzm_asic_wire_ids[index];
        uint32_t actual = 0;
        if (!ops->read_u32(ops_context, asic_id, BZM_BRINGUP_CONTROL_ENGINE_ID, BZM_LOCAL_REG_UART_TDM_CONTROL, &actual)) {
            return set_report(report, BZM_BRINGUP_REASON_IO, asic_id, 0, BZM_LOCAL_REG_UART_TDM_CONTROL,
                              tdm_control & ~1U, 0);
        }
        if (actual != (tdm_control & ~1U)) {
            return set_report(report, BZM_BRINGUP_REASON_REGISTER_READBACK, asic_id, 0,
                              BZM_LOCAL_REG_UART_TDM_CONTROL, tdm_control & ~1U, actual);
        }
    }

    for (uint8_t index = 0; index < BZM_BRINGUP_ASIC_COUNT; ++index) {
        uint8_t asic_id = bzm_asic_wire_ids[index];
        for (uint8_t pll = 0; pll < BZM_PLL_COUNT; ++pll) {
            uint8_t enable_register = pll_register(pll, BZM_LOCAL_REG_PLL0_ENABLE);
            if (!ops->write_u32(ops_context, asic_id, BZM_BRINGUP_CONTROL_ENGINE_ID, enable_register, 0)) {
                return set_report(report, BZM_BRINGUP_REASON_IO, asic_id, pll, enable_register, 0, 0);
            }
        }
        for (uint8_t pll = 0; pll < BZM_PLL_COUNT; ++pll) {
            uint8_t feedback_register = pll_register(pll, BZM_LOCAL_REG_PLL0_FBDIV);
            uint8_t postdiv_register = pll_register(pll, BZM_LOCAL_REG_PLL0_POSTDIV);
            if (!ops->write_u32(ops_context, asic_id, BZM_BRINGUP_CONTROL_ENGINE_ID, feedback_register,
                                BZM_PLL_800_FBDIV) ||
                !ops->write_u32(ops_context, asic_id, BZM_BRINGUP_CONTROL_ENGINE_ID, postdiv_register, BZM_PLL_800_POSTDIV)) {
                return set_report(report, BZM_BRINGUP_REASON_IO, asic_id, pll, feedback_register,
                                  BZM_PLL_800_FBDIV, 0);
            }
        }
        ops->delay_ms(ops_context, 1);
        for (uint8_t pll = 0; pll < BZM_PLL_COUNT; ++pll) {
            uint8_t enable_register = pll_register(pll, BZM_LOCAL_REG_PLL0_ENABLE);
            if (!ops->write_u32(ops_context, asic_id, BZM_BRINGUP_CONTROL_ENGINE_ID, enable_register, BZM_PLL_ENABLE_VALUE)) {
                return set_report(report, BZM_BRINGUP_REASON_IO, asic_id, pll, enable_register,
                                  BZM_PLL_ENABLE_VALUE, 0);
            }
        }

        for (uint8_t pll = 0; pll < BZM_PLL_COUNT; ++pll) {
            const bzm_register_value_t expected[] = {
                {pll_register(pll, BZM_LOCAL_REG_PLL0_FBDIV), BZM_PLL_800_FBDIV},
                {pll_register(pll, BZM_LOCAL_REG_PLL0_POSTDIV), BZM_PLL_800_POSTDIV},
            };
            for (size_t item = 0; item < sizeof(expected) / sizeof(expected[0]); ++item) {
                uint32_t actual = 0;
                if (!ops->read_u32(ops_context, asic_id, BZM_BRINGUP_CONTROL_ENGINE_ID, expected[item].offset, &actual)) {
                    return set_report(report, BZM_BRINGUP_REASON_IO, asic_id, pll, expected[item].offset,
                                      expected[item].value, 0);
                }
                if (actual != expected[item].value) {
                    return set_report(report, BZM_BRINGUP_REASON_REGISTER_READBACK, asic_id, pll,
                                      expected[item].offset, expected[item].value, actual);
                }
            }
        }

        bool locked[BZM_PLL_COUNT] = {false, false};
        uint32_t enable_values[BZM_PLL_COUNT] = {0, 0};
        for (uint8_t attempt = 0; attempt < BZM_PLL_LOCK_ATTEMPTS && !(locked[0] && locked[1]); ++attempt) {
            for (uint8_t pll = 0; pll < BZM_PLL_COUNT; ++pll) {
                uint8_t enable_register = pll_register(pll, BZM_LOCAL_REG_PLL0_ENABLE);
                if (!ops->read_u32(ops_context, asic_id, BZM_BRINGUP_CONTROL_ENGINE_ID, enable_register, &enable_values[pll])) {
                    return set_report(report, BZM_BRINGUP_REASON_IO, asic_id, pll, enable_register,
                                      BZM_PLL_LOCK_MASK, 0);
                }
                locked[pll] = (enable_values[pll] & BZM_PLL_LOCK_MASK) == BZM_PLL_LOCK_MASK;
            }
            if (!(locked[0] && locked[1])) {
                ops->delay_ms(ops_context, BZM_PLL_LOCK_POLL_MS);
            }
        }
        for (uint8_t pll = 0; pll < BZM_PLL_COUNT; ++pll) {
            if (!locked[pll]) {
                uint8_t enable_register = pll_register(pll, BZM_LOCAL_REG_PLL0_ENABLE);
                return set_report(report, BZM_BRINGUP_REASON_PLL_UNLOCKED, asic_id, pll, enable_register,
                                  BZM_PLL_LOCK_MASK, enable_values[pll] & BZM_PLL_LOCK_MASK);
            }
        }

        const bzm_register_value_t dll_disabled[] = {
            {BZM_LOCAL_REG_DLL0_CONTROL_5, 0},
            {BZM_LOCAL_REG_DLL1_CONTROL_5, 0},
        };
        bzm_bringup_outcome_t dll_outcome = write_and_verify_registers(
            ops, ops_context, asic_id, dll_disabled, sizeof(dll_disabled) / sizeof(dll_disabled[0]), report);
        if (dll_outcome != BZM_BRINGUP_GOOD) {
            return dll_outcome;
        }

        /* PLL programming must not disturb the sensor/TDM path used to
         * produce the lock and voltage evidence below. Re-prove the known
         * reference controls after both PLLs have locked. */
        const bzm_register_value_t sensor_controls[] = {
            {BZM_LOCAL_REG_UART_TDM_CONTROL, tdm_control & ~1U},
            {BZM_LOCAL_REG_SLOW_CLOCK_DIVIDER, BZM_REFERENCE_SLOW_CLOCK_DIVIDER},
            {BZM_LOCAL_REG_TDM_DELAY, BZM_REFERENCE_TDM_DELAY},
            {BZM_LOCAL_REG_UART_TX, 0x0f},
            {BZM_LOCAL_REG_SENSOR_TDM_GAP_COUNT, BZM_REFERENCE_SENSOR_TDM_GAP_COUNT},
            {BZM_LOCAL_REG_SENSOR_CLOCK_DIVIDER, (BZM_REFERENCE_SENSOR_DIVIDER << 5) | BZM_REFERENCE_SENSOR_DIVIDER},
        };
        for (size_t item = 0; item < sizeof(sensor_controls) / sizeof(sensor_controls[0]); ++item) {
            uint32_t actual = 0;
            if (!ops->read_u32(ops_context, asic_id, BZM_BRINGUP_CONTROL_ENGINE_ID, sensor_controls[item].offset, &actual)) {
                return set_report(report, BZM_BRINGUP_REASON_IO, asic_id, 0, sensor_controls[item].offset,
                                  sensor_controls[item].value, 0);
            }
            if (actual != sensor_controls[item].value) {
                return set_report(report, BZM_BRINGUP_REASON_REGISTER_READBACK, asic_id, 0,
                                  sensor_controls[item].offset, sensor_controls[item].value, actual);
            }
        }
    }

    if (!ops->write_u32(ops_context, BZM_ALL_ASICS, BZM_BRINGUP_CONTROL_ENGINE_ID, BZM_LOCAL_REG_UART_TDM_CONTROL, tdm_control)) {
        return set_report(report, BZM_BRINGUP_REASON_IO, BZM_ALL_ASICS, 0, BZM_LOCAL_REG_UART_TDM_CONTROL,
                          tdm_control, 0);
    }

    uint64_t clocks_configured_us = ops->now_us(ops_context);
    bzm_bringup_outcome_t outcome = verify_telemetry(ops, ops_context, telemetry_policy, clocks_configured_us, true, 100, report);
    if (outcome != BZM_BRINGUP_GOOD) {
        return outcome;
    }

    set_all_domain_clocks(state, BZM_FREQUENCY_POWER_ON_MHZ);
    return set_report(report, BZM_BRINGUP_REASON_NONE, BZM_LAST_ASIC_ID, 1, 0, 800, 800);
}

static bzm_bringup_outcome_t live_frequency_report(
    bzm_bringup_report_t *report,
    bzm_bringup_reason_t reason, size_t asic, size_t pll, uint8_t offset,
    uint32_t expected, uint32_t actual)
{
    return set_report(
        report, reason,
        asic < BZM_BRINGUP_ASIC_COUNT ? bzm_asic_wire_ids[asic]
                                      : BZM_ALL_ASICS,
        (uint8_t)pll, offset, expected, actual);
}

static bzm_bringup_outcome_t verify_live_frequency_domain(
    const bzm_bringup_ops_t *ops, void *ops_context, size_t asic, size_t pll,
    const bzm_frequency_target_t *target, bzm_bringup_report_t *report)
{
    const uint8_t asic_id = bzm_asic_wire_ids[asic];
    const bzm_register_value_t expected[] = {
        {pll_register((uint8_t)pll, BZM_LOCAL_REG_PLL0_FBDIV),
         target->feedback_divider},
        {pll_register((uint8_t)pll, BZM_LOCAL_REG_PLL0_POSTDIV),
         target->postdiv_register},
    };
    for (size_t item = 0; item < sizeof(expected) / sizeof(expected[0]);
         ++item) {
        uint32_t actual = 0;
        if (!ops->read_u32(ops_context, asic_id,
                           BZM_BRINGUP_CONTROL_ENGINE_ID,
                           expected[item].offset, &actual)) {
            return live_frequency_report(
                report, BZM_BRINGUP_REASON_IO, asic, pll,
                expected[item].offset, expected[item].value, 0);
        }
        if (actual != expected[item].value) {
            return live_frequency_report(
                report,
                BZM_BRINGUP_REASON_REGISTER_READBACK, asic, pll,
                expected[item].offset, expected[item].value, actual);
        }
    }

    const uint8_t enable_register =
        pll_register((uint8_t)pll, BZM_LOCAL_REG_PLL0_ENABLE);
    uint32_t enable_value = 0;
    for (uint8_t attempt = 0; attempt < 30; ++attempt) {
        if (!ops->read_u32(ops_context, asic_id,
                           BZM_BRINGUP_CONTROL_ENGINE_ID, enable_register,
                           &enable_value)) {
            return live_frequency_report(
                report, BZM_BRINGUP_REASON_IO, asic, pll,
                enable_register, BZM_PLL_LOCK_MASK, 0);
        }
        if ((enable_value & BZM_PLL_LOCK_MASK) == BZM_PLL_LOCK_MASK) {
            return BZM_BRINGUP_GOOD;
        }
        if (ops->delay_ms != NULL) ops->delay_ms(ops_context, 100);
    }
    return live_frequency_report(
        report, BZM_BRINGUP_REASON_PLL_UNLOCKED, asic, pll,
        enable_register, BZM_PLL_LOCK_MASK,
        enable_value & BZM_PLL_LOCK_MASK);
}

static bzm_bringup_outcome_t program_live_frequency_domain(
    const bzm_bringup_ops_t *ops, void *ops_context, size_t asic, size_t pll,
    const bzm_frequency_target_t *target, bzm_bringup_report_t *report)
{
    const uint8_t asic_id = bzm_asic_wire_ids[asic];
    const uint8_t feedback_register =
        pll_register((uint8_t)pll, BZM_LOCAL_REG_PLL0_FBDIV);
    const uint8_t postdiv_register =
        pll_register((uint8_t)pll, BZM_LOCAL_REG_PLL0_POSTDIV);
    if (!ops->write_u32(ops_context, asic_id,
                        BZM_BRINGUP_CONTROL_ENGINE_ID, feedback_register,
                        target->feedback_divider) ||
        !ops->write_u32(ops_context, asic_id,
                        BZM_BRINGUP_CONTROL_ENGINE_ID, postdiv_register,
                        target->postdiv_register)) {
        return live_frequency_report(
            report, BZM_BRINGUP_REASON_IO, asic, pll,
            feedback_register, target->feedback_divider, 0);
    }

    /* Live tuning leaves the PLL enabled and waits 1 ms after changing dividers. */
    if (ops->delay_ms != NULL) ops->delay_ms(ops_context, 1);
    return verify_live_frequency_domain(
        ops, ops_context, asic, pll, target, report);
}

static bzm_bringup_outcome_t program_live_broadcast_shortcut(
    const bzm_bringup_ops_t *ops, void *ops_context,
    const bzm_frequency_target_t *target, bzm_bringup_report_t *report)
{
    for (size_t pll = 0; pll < BZM_BRINGUP_PLL_COUNT; ++pll) {
        const uint8_t feedback_register =
            pll_register((uint8_t)pll, BZM_LOCAL_REG_PLL0_FBDIV);
        const uint8_t postdiv_register =
            pll_register((uint8_t)pll, BZM_LOCAL_REG_PLL0_POSTDIV);
        if (!ops->write_u32(
                ops_context, BZM_ALL_ASICS, BZM_BRINGUP_CONTROL_ENGINE_ID,
                feedback_register, target->feedback_divider) ||
            !ops->write_u32(
                ops_context, BZM_ALL_ASICS, BZM_BRINGUP_CONTROL_ENGINE_ID,
                postdiv_register, target->postdiv_register)) {
            return live_frequency_report(
                report, BZM_BRINGUP_REASON_IO,
                BZM_BRINGUP_ASIC_COUNT, pll, feedback_register,
                target->feedback_divider, 0);
        }
        if (ops->delay_ms != NULL) ops->delay_ms(ops_context, 1);
    }

    for (size_t asic = 0; asic < BZM_BRINGUP_ASIC_COUNT; ++asic) {
        for (size_t pll = 0; pll < BZM_BRINGUP_PLL_COUNT; ++pll) {
            bzm_bringup_outcome_t outcome = verify_live_frequency_domain(
                ops, ops_context, asic, pll, target, report);
            if (outcome != BZM_BRINGUP_GOOD) return outcome;
        }
    }
    return BZM_BRINGUP_GOOD;
}

static bzm_bringup_outcome_t live_frequency_domains_step_once(
    bzm_bringup_state_t *state, const bzm_bringup_ops_t *ops,
    void *ops_context,
    const float
        target_mhz[BZM_BRINGUP_ASIC_COUNT][BZM_BRINGUP_PLL_COUNT],
    bool allow_initial_jump, bzm_bringup_report_t *report)
{
    bzm_frequency_target_t
        targets[BZM_BRINGUP_ASIC_COUNT][BZM_BRINGUP_PLL_COUNT];
    bool changed[BZM_BRINGUP_ASIC_COUNT][BZM_BRINGUP_PLL_COUNT] = {0};
    float shortcut_target = 0.0f;
    if (state == NULL || !basic_ops_are_valid(ops) || target_mhz == NULL ||
        !state->running) {
        return live_frequency_report(
            report,
            BZM_BRINGUP_REASON_INVALID_ARGUMENT, BZM_BRINGUP_ASIC_COUNT, 0,
            0, 0, 0);
    }

    for (size_t asic = 0; asic < BZM_BRINGUP_ASIC_COUNT; ++asic) {
        for (size_t pll = 0; pll < BZM_BRINGUP_PLL_COUNT; ++pll) {
            const float current = state->domain_clock_mhz[asic][pll];
            if (!bzm_frequency_request_is_valid(current) ||
                !bzm_frequency_resolve_target(target_mhz[asic][pll],
                                              &targets[asic][pll]) ||
                fabsf(targets[asic][pll].actual_mhz -
                       target_mhz[asic][pll]) >= 0.001f) {
                return live_frequency_report(
                    report,
                    BZM_BRINGUP_REASON_INVALID_ARGUMENT, asic, pll, 0,
                    (uint32_t)lroundf(current),
                    (uint32_t)lroundf(isfinite(target_mhz[asic][pll])
                                         ? target_mhz[asic][pll]
                                         : 0.0f));
            }

            const float delta =
                fabsf(targets[asic][pll].actual_mhz - current);
            changed[asic][pll] = delta >= 0.001f;
            if (allow_initial_jump) {
                if (fabsf(current - BZM_FREQUENCY_POWER_ON_MHZ) >= 0.001f ||
                    targets[asic][pll].actual_mhz <
                        BZM_FREQUENCY_POWER_ON_MHZ ||
                    targets[asic][pll].actual_mhz >
                        BZM_FREQUENCY_INITIAL_MAX_MHZ) {
                    return live_frequency_report(
                        report,
                        BZM_BRINGUP_REASON_INVALID_ARGUMENT, asic, pll, 0,
                        (uint32_t)BZM_FREQUENCY_POWER_ON_MHZ,
                        (uint32_t)lroundf(target_mhz[asic][pll]));
                }
                if (asic == 0 && pll == 0) {
                    shortcut_target = targets[asic][pll].actual_mhz;
                } else if (fabsf(shortcut_target -
                                 targets[asic][pll].actual_mhz) >= 0.001f) {
                    return live_frequency_report(
                        report,
                        BZM_BRINGUP_REASON_INVALID_ARGUMENT, asic, pll, 0,
                        (uint32_t)lroundf(shortcut_target),
                        (uint32_t)lroundf(target_mhz[asic][pll]));
                }
            } else if (changed[asic][pll] &&
                       delta > BZM_FREQUENCY_RAMP_STEP_MHZ + 0.001f) {
                return live_frequency_report(
                    report,
                    BZM_BRINGUP_REASON_INVALID_ARGUMENT, asic, pll, 0,
                    (uint32_t)lroundf(current),
                    (uint32_t)lroundf(target_mhz[asic][pll]));
            }
        }
    }

    if (allow_initial_jump &&
        shortcut_target > BZM_FREQUENCY_POWER_ON_MHZ) {
        bzm_bringup_outcome_t outcome = program_live_broadcast_shortcut(
            ops, ops_context, &targets[0][0], report);
        if (outcome != BZM_BRINGUP_GOOD) return outcome;
        set_all_domain_clocks(state, shortcut_target);
    } else {
        for (size_t asic = 0; asic < BZM_BRINGUP_ASIC_COUNT; ++asic) {
            for (size_t pll = 0; pll < BZM_BRINGUP_PLL_COUNT; ++pll) {
                if (changed[asic][pll]) {
                    bzm_bringup_outcome_t outcome =
                        program_live_frequency_domain(
                            ops, ops_context, asic, pll, &targets[asic][pll],
                            report);
                    if (outcome != BZM_BRINGUP_GOOD) {
                        state->clock_mhz = average_domain_clocks(state);
                        return outcome;
                    }
                    state->domain_clock_mhz[asic][pll] =
                        targets[asic][pll].actual_mhz;
                }
            }
        }
        state->clock_mhz = average_domain_clocks(state);
    }

    return live_frequency_report(
        report, BZM_BRINGUP_REASON_NONE,
        BZM_BRINGUP_ASIC_COUNT - 1U, BZM_BRINGUP_PLL_COUNT - 1U, 0,
        (uint32_t)lroundf(state->clock_mhz),
        (uint32_t)lroundf(state->clock_mhz));
}

bzm_bringup_outcome_t bzm_bringup_live_frequency_domains_step(
    bzm_bringup_state_t *state, const bzm_bringup_ops_t *ops,
    void *ops_context,
    const float
        target_mhz[BZM_BRINGUP_ASIC_COUNT][BZM_BRINGUP_PLL_COUNT],
    bool allow_initial_jump, bzm_bringup_report_t *report)
{
    enum {
        LIVE_FREQUENCY_TRANSIENT_ATTEMPTS = 3,
        LIVE_FREQUENCY_RETRY_DELAY_MS = 10,
    };
    bzm_bringup_report_t local_report = {0};
    bzm_bringup_report_t *attempt_report =
        report == NULL ? &local_report : report;

    for (uint8_t attempt = 0;
         attempt < LIVE_FREQUENCY_TRANSIENT_ATTEMPTS; ++attempt) {
        bzm_bringup_outcome_t outcome =
            live_frequency_domains_step_once(
                state, ops, ops_context, target_mhz, allow_initial_jump,
                attempt_report);
        const bool transient = outcome == BZM_BRINGUP_BAD &&
            (attempt_report->reason == BZM_BRINGUP_REASON_IO ||
             attempt_report->reason ==
                 BZM_BRINGUP_REASON_REGISTER_READBACK);
        if (!transient ||
            attempt + 1 == LIVE_FREQUENCY_TRANSIENT_ATTEMPTS) {
            return outcome;
        }
        if (ops != NULL && ops->delay_ms != NULL) {
            ops->delay_ms(ops_context, LIVE_FREQUENCY_RETRY_DELAY_MS);
        }
    }
    return BZM_BRINGUP_BAD;
}

static bzm_bringup_outcome_t bzm_bringup_activate_engines(const bzm_bringup_ops_t * ops,
                                                      void * ops_context, const bzm_bringup_telemetry_policy_t * telemetry_policy,
                                                      bzm_bringup_report_t * report)
{
    /* BIRDS disables the result FSM before turning TDM off. engine activation pauses
     * TDM around every sentinel activation batch, so leaving result reports
     * enabled here permits raw, headerless eight-byte result/status packets
     * to escape while the framed transport is paused. Keep reports disabled
     * for the complete ramp; mining enables them only after TDM is restored. */
    const bzm_register_value_t result_reporting_disabled[] = {
        {BZM_LOCAL_REG_RESULT_STATUS_CONTROL, BZM_RESULT_REPORT_DISABLED},
    };
    for (uint8_t index = 0; index < BZM_BRINGUP_ASIC_COUNT; ++index) {
        uint8_t asic_id = bzm_asic_wire_ids[index];
        bzm_bringup_outcome_t result_status =
            write_and_verify_registers(ops, ops_context, asic_id, result_reporting_disabled,
                                       sizeof(result_reporting_disabled) / sizeof(result_reporting_disabled[0]), report);
        if (result_status != BZM_BRINGUP_GOOD) {
            return result_status;
        }
    }

    for (uint16_t pair_index = 0; pair_index < BZM_TOPOLOGY_PAIR_COUNT; ++pair_index) {
        bzm_engine_pair_t pair;
        if (!bzm_topology_balanced_pair_at(pair_index, &pair)) {
            return set_report(report, BZM_BRINGUP_REASON_TOPOLOGY, 0, 0, 0, pair_index, 0);
        }
        if (!ops->balanced_batch_begin(ops_context, pair_index)) {
            return set_report(report, BZM_BRINGUP_REASON_BALANCED_BATCH, 0, 0, 0, 1, 0);
        }
        for (uint8_t asic_index = 0; asic_index < BZM_BRINGUP_ASIC_COUNT; ++asic_index) {
            uint8_t asic_id = bzm_asic_wire_ids[asic_index];
            if (!ops->balanced_pair_commit(ops_context, asic_id, &pair)) {
                return set_report(report, BZM_BRINGUP_REASON_BALANCED_PAIR_COMMIT, asic_id, 0, 0, pair_index, 0);
            }
        }
        if (!ops->balanced_batch_end(ops_context, pair_index)) {
            return set_report(report, BZM_BRINGUP_REASON_BALANCED_BATCH, 0, 0, 0, 1, 0);
        }

        /* Timestamp only after all four ASIC commits in this balanced
         * batch. A cached sample from before any commit must not prove that
         * the newly activated pair is electrically safe and clock locked. */
        uint64_t committed_after_us = ops->now_us(ops_context);
        bzm_bringup_outcome_t telemetry_outcome =
            verify_telemetry(ops, ops_context, telemetry_policy, committed_after_us, true, 30, report);
        if (telemetry_outcome != BZM_BRINGUP_GOOD) {
            return telemetry_outcome;
        }
    }
    if (!ops->activation_barrier(ops_context)) {
        return set_report(report, BZM_BRINGUP_REASON_ACTIVATION_BARRIER, 0, 0, 0, 1, 0);
    }

    return set_report(report, BZM_BRINGUP_REASON_NONE, BZM_LAST_ASIC_ID, 0, 0,
                      BZM_TOPOLOGY_PAIR_COUNT * BZM_BRINGUP_ASIC_COUNT, BZM_TOPOLOGY_PAIR_COUNT * BZM_BRINGUP_ASIC_COUNT);
}

static bzm_bringup_outcome_t bzm_bringup_enable_results(bzm_bringup_state_t * state, const bzm_bringup_ops_t * ops, void * ops_context,
                                                const bzm_bringup_telemetry_policy_t * telemetry_policy,
                                                bzm_bringup_report_t * report)
{
    state->running = false;
    /* TDM is now continuously enabled. Only at this boundary may the ASIC
     * result FSM be opened, so every mining result uses the addressed TDM
     * frame consumed by bzm_frame_parser. */
    const bzm_register_value_t result_reporting_enabled[] = {
        {BZM_LOCAL_REG_RESULT_STATUS_CONTROL, BZM_RESULT_REPORT_ENABLED},
    };
    for (uint8_t index = 0; index < BZM_BRINGUP_ASIC_COUNT; ++index) {
        uint8_t asic_id = bzm_asic_wire_ids[index];
        bzm_bringup_outcome_t outcome =
            write_and_verify_registers(ops, ops_context, asic_id, result_reporting_enabled,
                                       sizeof(result_reporting_enabled) / sizeof(result_reporting_enabled[0]), report);
        if (outcome != BZM_BRINGUP_GOOD) {
            return outcome;
        }
    }
    uint64_t result_reporting_enabled_us = ops->now_us(ops_context);
    bzm_bringup_outcome_t outcome = verify_telemetry(ops, ops_context, telemetry_policy, result_reporting_enabled_us, true, 100, report);
    if (outcome != BZM_BRINGUP_GOOD) {
        return outcome;
    }
    state->running = true;
    return set_report(report, BZM_BRINGUP_REASON_NONE, BZM_LAST_ASIC_ID, 0, 0, 1, 1);
}

bzm_bringup_outcome_t bzm_bringup_start(bzm_bringup_state_t *state, const bzm_bringup_ops_t *ops,
                                       void *context, const bzm_bringup_telemetry_policy_t *policy,
                                       bzm_bringup_report_t *report)
{
    if (state == NULL || !basic_ops_are_valid(ops) || ops->probe_noop == NULL ||
        ops->delay_ms == NULL || ops->now_us == NULL || ops->telemetry_snapshot == NULL ||
        ops->balanced_batch_begin == NULL || ops->balanced_pair_commit == NULL ||
        ops->balanced_batch_end == NULL || ops->activation_barrier == NULL ||
        !telemetry_policy_is_valid(policy)) {
        return set_report(report, BZM_BRINGUP_REASON_INVALID_ARGUMENT, 0, 0, 0, 0, 0);
    }
    bzm_bringup_init(state);
    if (bzm_bringup_discover_chain(ops, context, report) != BZM_BRINGUP_GOOD ||
        bzm_bringup_configure_sensors(ops, context, policy, report) != BZM_BRINGUP_GOOD ||
        bzm_bringup_configure_clocks(state, ops, context, policy, report) != BZM_BRINGUP_GOOD ||
        bzm_bringup_activate_engines(ops, context, policy, report) != BZM_BRINGUP_GOOD ||
        bzm_bringup_enable_results(state, ops, context, policy, report) != BZM_BRINGUP_GOOD) {
        return BZM_BRINGUP_BAD;
    }
    return BZM_BRINGUP_GOOD;
}
