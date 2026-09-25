#ifndef BZM_FREQUENCY_H
#define BZM_FREQUENCY_H

#include <stdbool.h>
#include <stdint.h>

/* Frequency-targeting constants for the two BZM hash PLLs. */
#define BZM_FREQUENCY_POWER_ON_MHZ 800.0f
#define BZM_FREQUENCY_TARGET_MIN_MHZ 800.0f
#define BZM_FREQUENCY_TARGET_MAX_MHZ 2000.0f
#define BZM_FREQUENCY_INITIAL_OFFSET_MHZ 100.0f
#define BZM_FREQUENCY_INITIAL_MAX_MHZ 1425.0f
#define BZM_FREQUENCY_RAMP_STEP_MHZ 25.0f
#define BZM_FREQUENCY_FINE_STEP_MHZ 6.25f

typedef struct
{
    float requested_mhz;
    float actual_mhz;
    uint16_t feedback_divider;
    uint32_t postdiv_register;
} bzm_frequency_target_t;

bool bzm_frequency_request_is_valid(float requested_mhz);
bool bzm_frequency_resolve_target(float requested_mhz,
                                  bzm_frequency_target_t *target);
float bzm_frequency_initial_mhz(float target_mhz);

#endif // BZM_FREQUENCY_H
