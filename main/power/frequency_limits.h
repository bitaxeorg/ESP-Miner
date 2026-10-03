#ifndef FREQUENCY_LIMITS_H_
#define FREQUENCY_LIMITS_H_

#include <stdbool.h>
#include <stddef.h>
#include <math.h>

// The existing shutdown ramp uses 50 MHz on every supported ASIC family.
#define ASIC_MIN_FREQUENCY_MHZ 50.0f

// Recovery must strictly reduce frequency without going below the shutdown
// clock. At the floor (or for invalid inputs), keep the ASIC powered off.
static inline bool asic_recovery_frequency(float current, float reduction, float *next)
{
    if (next == NULL || !isfinite(current) || !isfinite(reduction) ||
        current <= ASIC_MIN_FREQUENCY_MHZ || reduction <= 0) {
        return false;
    }
    float reduced = fmaxf(ASIC_MIN_FREQUENCY_MHZ, current - reduction);
    if (reduced >= current) return false;
    *next = reduced;
    return true;
}

#endif /* FREQUENCY_LIMITS_H_ */
