#ifndef FREQUENCY_TRANSITION_H
#define FREQUENCY_TRANSITION_H

#include <stdbool.h>

extern const char *FREQUENCY_TRANSITION_TAG;

/**
 * @brief Function pointer type for ASIC hash frequency setting functions
 * 
 * This type defines the signature for functions that set the hash frequency
 * for different ASIC types.
 * 
 * @param frequency The frequency to set in MHz
 */
typedef void (*set_hash_frequency_fn)(float frequency);

/**
 * @brief Transition the ASIC frequency to a target value
 * 
 * This function gradually adjusts the ASIC frequency to reach the target value,
 * stepping up or down in increments to ensure stability.
 * 
 * @param target_frequency The target frequency in MHz
 * @param set_frequency_fn Function pointer to the appropriate ASIC's set_hash_frequency function
 * @param asic_type The type of ASIC chip (for logging purposes only)
 */
void do_frequency_transition(float target_frequency, set_hash_frequency_fn set_frequency_fn);

#endif // FREQUENCY_TRANSITION_H
