#ifndef BZM_LEASE_GUARD_H
#define BZM_LEASE_GUARD_H

#include <stdbool.h>
#include <stdint.h>

#include "bzm/bridge.h"

/* Pure status predicate used by the board startup adapter. */
bool bzm_lease_guard_status_is_controlled(const bzm_bridge_safety_status_t * status);

/* A powered validation operation is allowed strictly before its absolute
 * execution deadline. Zero is always fail-closed. */
bool bzm_lease_guard_deadline_allows(uint64_t deadline_ms, uint64_t now_ms);

/* Constructs an absolute deadline without unsigned wraparound. */
bool bzm_lease_guard_make_deadline(uint64_t now_ms, uint32_t lease_ms, uint64_t * deadline_ms);

#endif // BZM_LEASE_GUARD_H
