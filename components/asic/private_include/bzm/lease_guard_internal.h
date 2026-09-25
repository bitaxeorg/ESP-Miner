#ifndef BZM_LEASE_GUARD_INTERNAL_H
#define BZM_LEASE_GUARD_INTERNAL_H

#include "bzm/lease_guard.h"

#define BZM_LEASE_GUARD_MAX_DELAY_CHUNK_MS 250U

typedef bool (*bzm_lease_guard_renew_fn)(void * context);
typedef void (*bzm_lease_guard_sleep_fn)(void * context, uint32_t delay_ms);

typedef struct
{
    bool renewed;
    uint64_t last_renewal_ms;
} bzm_lease_guard_schedule_t;

/* Sleep in bounded chunks, renewing immediately before every chunk. */
bool bzm_lease_guard_delay(uint32_t delay_ms, bzm_lease_guard_renew_fn renew, bzm_lease_guard_sleep_fn sleep, void * context);

/* Service a lease from inside a long-running operation without issuing a
 * heartbeat on every checkpoint. The first call always renews. Later calls
 * renew when the interval has elapsed; a backwards clock also renews
 * fail-safe. A failed renewal is never recorded as successful. */
bool bzm_lease_guard_service_due(bzm_lease_guard_schedule_t * schedule, uint64_t now_ms, uint32_t interval_ms,
                                 bzm_lease_guard_renew_fn renew, void * context);

/* Authorization belongs to the operation being serviced. This wrapper keeps
 * a completed bring-up execution deadline from being confused with a live
 * mining dispatch lease. */
bool bzm_lease_guard_service_authorized(bzm_lease_guard_schedule_t * schedule, bool authorized, uint64_t now_ms,
                                        uint32_t interval_ms, bzm_lease_guard_renew_fn renew, void * context);

#endif // BZM_LEASE_GUARD_INTERNAL_H
