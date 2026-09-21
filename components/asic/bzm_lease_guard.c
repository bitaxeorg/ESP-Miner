#include "bzm/lease_guard_internal.h"

#include <stddef.h>
#include <stdint.h>

bool bzm_lease_guard_status_is_controlled(const bzm_bridge_safety_status_t * status)
{
    const uint16_t required_evidence =
        BZM_BRIDGE_SAFETY_EVIDENCE_LEASE_VALID | BZM_BRIDGE_SAFETY_EVIDENCE_TRIP_CLEAR | BZM_BRIDGE_SAFETY_EVIDENCE_FAULT_CLEAR;
    return status != NULL && status->valid && status->state == BZM_BRIDGE_SAFETY_STATE_CONTROLLED &&
           status->fault == BZM_BRIDGE_SAFETY_FAULT_NONE && status->runtime_verdict == BZM_BRIDGE_SAFETY_RUNTIME_GOOD_CONTROLLED &&
           status->lease_remaining_ms > 0 && (status->evidence & required_evidence) == required_evidence &&
           !status->trip_input_asserted;
}

bool bzm_lease_guard_delay(uint32_t delay_ms, bzm_lease_guard_renew_fn renew, bzm_lease_guard_sleep_fn sleep, void * context)
{
    if (renew == NULL || sleep == NULL) {
        return false;
    }
    while (delay_ms != 0) {
        if (!renew(context)) {
            return false;
        }
        uint32_t chunk = delay_ms > BZM_LEASE_GUARD_MAX_DELAY_CHUNK_MS ? BZM_LEASE_GUARD_MAX_DELAY_CHUNK_MS : delay_ms;
        sleep(context, chunk);
        delay_ms -= chunk;
    }
    return true;
}

bool bzm_lease_guard_service_due(bzm_lease_guard_schedule_t * schedule, uint64_t now_ms, uint32_t interval_ms,
                                 bzm_lease_guard_renew_fn renew, void * context)
{
    if (schedule == NULL || interval_ms == 0 || renew == NULL) {
        return false;
    }
    bool due = !schedule->renewed || now_ms < schedule->last_renewal_ms ||
               now_ms - schedule->last_renewal_ms >= interval_ms;
    if (!due) {
        return true;
    }
    if (!renew(context)) {
        return false;
    }
    schedule->renewed = true;
    schedule->last_renewal_ms = now_ms;
    return true;
}

bool bzm_lease_guard_service_authorized(bzm_lease_guard_schedule_t * schedule, bool authorized, uint64_t now_ms,
                                        uint32_t interval_ms, bzm_lease_guard_renew_fn renew, void * context)
{
    return authorized && bzm_lease_guard_service_due(schedule, now_ms, interval_ms, renew, context);
}

bool bzm_lease_guard_deadline_allows(uint64_t deadline_ms, uint64_t now_ms)
{
    return deadline_ms != 0 && now_ms < deadline_ms;
}

bool bzm_lease_guard_make_deadline(uint64_t now_ms, uint32_t lease_ms, uint64_t * deadline_ms)
{
    if (deadline_ms == NULL || lease_ms == 0 || now_ms > UINT64_MAX - lease_ms) {
        return false;
    }
    *deadline_ms = now_ms + lease_ms;
    return *deadline_ms != 0;
}
