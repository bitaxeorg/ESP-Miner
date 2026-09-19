#include "bonanza_power_policy.h"

#include <math.h>
#include <stddef.h>

static bool stop(bonanza_power_policy_t *policy)
{
    policy->running = false;
    policy->stopped = policy->operations.stop(policy->operations.context);
    if (!policy->stopped) policy->fault = true;
    return policy->stopped;
}

static bool cancelled(bonanza_power_policy_t *policy)
{
    return policy->operations.cancelled != NULL &&
        policy->operations.cancelled(policy->operations.context);
}

static void begin_cooling(bonanza_power_policy_t *policy, uint64_t now_ms, bonanza_power_target_t target)
{
    policy->cooling = true;
    policy->cooling_since_ms = now_ms;
    policy->cooling_target = target;
    policy->reduced_target_saved = false;
    policy->operations.overheat(policy->operations.context, true);
    (void)stop(policy);
}

bool bonanza_power_policy_init(bonanza_power_policy_t *policy, bonanza_power_operations_t operations)
{
    if (policy == NULL || operations.start == NULL || operations.stop == NULL ||
        operations.apply == NULL || operations.maintenance == NULL ||
        operations.overheat == NULL || operations.save_target == NULL ||
        operations.minimum_voltage_mv == 0 ||
        !isfinite(operations.minimum_frequency_mhz) || operations.minimum_frequency_mhz <= 0) {
        return false;
    }
    *policy = (bonanza_power_policy_t){.operations = operations};
    return true;
}

bool bonanza_power_policy_pause(bonanza_power_policy_t *policy)
{
    policy->paused = true;
    if (policy->owner != BONANZA_POWER_OWNER_NONE) return false;
    return stop(policy);
}

bool bonanza_power_policy_resume(bonanza_power_policy_t *policy)
{
    if (!policy->ready || policy->fault || policy->owner != BONANZA_POWER_OWNER_NONE) return false;
    policy->paused = false;
    if (policy->cooling || policy->pool_unavailable) return true;
    if (policy->running) return true;
    bonanza_power_start_result_t started = policy->operations.start(policy->operations.context);
    if (started == BONANZA_POWER_START_OVERHEAT && !cancelled(policy)) {
        /* The next step establishes the cooldown clock from fresh evidence.
         * Startup is still unsuccessful and cannot publish RUNNING. */
        begin_cooling(policy, UINT64_MAX, policy->target);
        return false;
    }
    if (started != BONANZA_POWER_START_OK || cancelled(policy)) {
        policy->fault = !cancelled(policy);
        (void)stop(policy);
        return false;
    }
    policy->running = true;
    policy->stopped = false;
    return true;
}

bool bonanza_power_policy_maintenance(bonanza_power_policy_t *policy, bonanza_power_owner_t owner, bool acquire)
{
    if (owner <= BONANZA_POWER_OWNER_NONE || owner > BONANZA_POWER_OWNER_RESTART) return false;
    if (acquire) {
        if (policy->owner != BONANZA_POWER_OWNER_NONE) return false;
        policy->paused = true;
        policy->running = false;
        /* The board must prove safe-off here too. Bridge recovery can use an
         * independent regulator readback when the bridge cannot respond. */
        if (!policy->operations.maintenance(policy->operations.context, owner, true)) {
            policy->fault = true;
            policy->stopped = false;
            return false;
        }
        policy->owner = owner;
        policy->stopped = true;
        return true;
    }
    if (policy->owner != owner) return false;
    bool released = policy->operations.maintenance(policy->operations.context, owner, false);
    policy->owner = BONANZA_POWER_OWNER_NONE;
    if (!released) {
        policy->fault = true;
        policy->stopped = false;
        return false;
    }
    /* Failed OTA never silently restarts the hardware. An explicit resume is
     * required, and an existing fault remains latched. */
    return true;
}

void bonanza_power_policy_step(bonanza_power_policy_t *policy, uint64_t now_ms,
                       bonanza_power_target_t target, bonanza_power_sample_t sample,
                       bool pools_unavailable, bool hardware_fault,
                       bool persisted_overheat)
{
    policy->target = target;
    policy->pool_unavailable = pools_unavailable;
    if (policy->owner != BONANZA_POWER_OWNER_NONE) return;

    if (hardware_fault || sample.health == BONANZA_POWER_HEALTH_FAULT) {
        policy->fault = true;
    }
    if (policy->fault) {
        if (!policy->stopped) (void)stop(policy);
        return;
    }

    if (!policy->cooling && (sample.health == BONANZA_POWER_HEALTH_OVERHEAT || persisted_overheat)) {
        begin_cooling(policy, now_ms, target);
        if (!policy->stopped) return;
    }
    if (policy->cooling) {
        if (policy->cooling_since_ms == UINT64_MAX) policy->cooling_since_ms = now_ms;
        if (!policy->stopped || !sample.vreg_valid || !isfinite(sample.vreg_c)) return;
        if (now_ms < policy->cooling_since_ms ||
            now_ms - policy->cooling_since_ms < 30000 || sample.vreg_c > 95.0f) return;
        if (!policy->reduced_target_saved) {
            bonanza_power_target_t reduced = policy->cooling_target;
            if (!isfinite(reduced.frequency_mhz) ||
                reduced.frequency_mhz < policy->operations.minimum_frequency_mhz ||
                reduced.voltage_mv < policy->operations.minimum_voltage_mv) {
                policy->fault = true;
                return;
            }
            reduced.voltage_mv = reduced.voltage_mv > policy->operations.minimum_voltage_mv + 100U
                ? reduced.voltage_mv - 100U : policy->operations.minimum_voltage_mv;
            reduced.frequency_mhz = fmaxf(policy->operations.minimum_frequency_mhz,
                                          reduced.frequency_mhz - 100.0f);
            if (!policy->operations.save_target(policy->operations.context, reduced)) {
                policy->fault = true;
                return;
            }
            policy->reduced_target_saved = true;
        }
        /* User pause, pool loss and maintenance take precedence over recovery. */
        if (!policy->ready || policy->paused || pools_unavailable) return;
        bonanza_power_start_result_t started = policy->operations.start(policy->operations.context);
        if (started == BONANZA_POWER_START_OVERHEAT && !cancelled(policy)) {
            begin_cooling(policy, now_ms, target);
            return;
        }
        if (started != BONANZA_POWER_START_OK || cancelled(policy)) {
            policy->fault = !cancelled(policy);
            (void)stop(policy);
            return;
        }
        policy->running = true;
        policy->stopped = false;
        policy->cooling = false;
        policy->operations.overheat(policy->operations.context, false);
        return;
    }

    if (policy->paused || pools_unavailable || !policy->ready) {
        if (!policy->stopped) (void)stop(policy);
        return;
    }
    if (!policy->running && !bonanza_power_policy_resume(policy)) return;
    if (!policy->operations.apply(policy->operations.context, target) || cancelled(policy)) {
        policy->fault = !cancelled(policy);
        (void)stop(policy);
    }
}
